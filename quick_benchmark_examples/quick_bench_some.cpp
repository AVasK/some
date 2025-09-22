// Copyright (C) Alexander Vaskov 2025
#include <concepts>
#include <cstdint> //ints
#include <cstring> //memcpy
#include <iostream>
#include <memory> // unique_ptr
#include <type_traits>
#include <utility> // forward, move, exchange

#define VX_UNREACHABLE() __builtin_unreachable()
#define VX_SOME_LOG(expr)
#define VX_HARDENED false

namespace vx {
using u16 = std::uint16_t;
using u8 = std::uint8_t;
namespace cfg {
/// ===== [ SBO storage configuration ] ======
struct SBO {
    vx::u16 size { 16 };
    vx::u16 alignment { alignof(std::max_align_t) };
};
struct some {
    SBO sbo {};
    bool copy {true};
    bool move {true};
    bool empty_state {true};
    bool check_empty {VX_HARDENED};
};
}// namespace cfg

/// ===== [ FWD Declarations ] =====
template <class Trait, typename T>
struct impl_for;

template <typename Trait, cfg::some>
struct some;

template <typename Trait, std::size_t SBO_capacity, std::size_t alignment>
struct storage_for;

namespace detail {
    template <typename> struct is_polymorphic : std::false_type {};
    
    template <typename Trait, cfg::some config> 
    struct is_polymorphic< some<Trait, config> > : std::true_type {};
} // namespace detail

template <typename T>
concept polymorphic = detail::is_polymorphic<std::remove_cvref_t<T>>::value;

namespace detail {
template <typename T>
struct remove_innermost_const_impl {
    using type = T;
};
template <typename T>
struct remove_innermost_const_impl<T const&> {
    using type = T&;
};
template <typename T>
struct remove_innermost_const_impl<T const&&> {
    using type = T&&;
};
template <typename T>
struct remove_innermost_const_impl<T const*> {
    using type = T*;  
};
template <typename T>
using remove_innermost_const = typename detail::remove_innermost_const_impl<T>::type;

template <typename Ptr>
concept pointer_like = std::is_pointer_v<Ptr> || requires (Ptr p) {
    { *p };
    { static_cast<bool>(p) };
    { p.operator->() } -> std::convertible_to<decltype( &*p )>;
};
}//namespace detail
template <typename... Ts> struct mix {};
template <typename T> 
concept rvalue = std::is_rvalue_reference_v<T&&> && !std::is_const_v<T>;
/// ===== [ IMPL ] =====
template <class Trait, typename T>
struct impl;
namespace detail {
    template <typename Trait>
    struct extract_first_trait_from {
        using type = Trait;
    };
    template <typename Trait, typename... Traits>
    struct extract_first_trait_from<mix<Trait, Traits...>> {
        using type = Trait;
    };
    template <typename>
    struct mixed_traits : std::false_type {};
    template <typename... Ts>
    struct mixed_traits<vx::mix<Ts...>> : std::true_type {};

    template <typename T>
    using remove_ref_or_ptr_t = std::conditional_t<std::is_reference_v<T>, 
        std::remove_reference_t<T>,
        std::conditional_t<std::is_pointer_v<T>,
            std::remove_pointer_t<T>,
            T> >;

    enum class opcode : vx::u8 {
        copy_into,
        move_into,
        fsome_move_sbo_into,
#if not VX_FSOME_ELIDE_VCALL_ON_MOVE
        fsome_move_ptr_into,
#endif
        cleanup
    };
}//namespace detail

template <typename T>
using first_trait_from = typename detail::extract_first_trait_from<T>::type;

namespace detail {
template <typename T>
constexpr bool is_sbo_eligible_with(u16 SBO_capacity, u16 SBO_alignment) {
    return sizeof(T) <= SBO_capacity //< fits into SBO buffer
        && alignof(T) <= SBO_alignment ///< and has lower alignment
        && (SBO_alignment % alignof(T) == 0)
        && (not std::is_move_constructible_v<T> || std::is_nothrow_move_constructible_v<T>);
}
}// namespace detail

struct trait {
    trait() = default;
    trait (trait const&) = delete;
    trait (trait &&) = delete;
    virtual ~trait()=default;
private:
    template <class Trait, typename T> friend struct impl_for;
    template <typename Trait, std::size_t, std::size_t> friend struct storage_for;
    virtual void* do_action(detail::opcode, [[maybe_unused]] void* buffer, cfg::SBO, [[maybe_unused]] void* extra=nullptr) { return nullptr; }
};

template <class Trait, typename T>
struct impl_for : Trait {
    using value_type = std::conditional_t<std::is_reference_v<T> || std::is_pointer_v<T>, 
        detail::remove_innermost_const<T>,
        T
    >;
    using Self = std::conditional_t<std::is_reference_v<value_type>, 
        std::remove_reference_t<value_type>,
        std::conditional_t<std::is_pointer_v<value_type>,
            std::remove_pointer_t<value_type>,
            value_type>
    >;
    constexpr impl_for() requires std::is_default_constructible_v<T> =default;

    impl_for(impl_for const&) = default;

    explicit impl_for(std::convertible_to<T> auto&& other) 
    noexcept(std::is_nothrow_constructible_v<value_type, decltype(other)>)
    : self_{std::forward<decltype(other)>(other)} {}

    const auto* operator->() const { return get(); }
    auto* operator->() { return get(); }

    auto* get() & { 
        if constexpr (std::is_pointer_v<value_type>) {
            return self_;
        } else if constexpr (detail::pointer_like<value_type>) {
            return &*self_;
        } else {
            return &self_;
        }
    }

    const auto* get() const& {
        if constexpr (std::is_pointer_v<value_type>) {
            return self_;
        } else if constexpr (detail::pointer_like<value_type>) {
            return &*self_;
        } else {
            return &self_;
        }
    }

    auto& self() { 
        return *get();
    }
    auto const& self() const { 
        return *get();
    }
private:
    [[no_unique_address]] value_type self_{};
protected:
    virtual void* do_action(detail::opcode op, [[maybe_unused]] void* buffer, [[maybe_unused]] cfg::SBO sbo, [[maybe_unused]] void* extra=nullptr) override {
        switch (op) {
            using enum detail::opcode;
            case copy_into: if constexpr (std::is_copy_constructible_v<Self>) {
                if constexpr (std::is_pointer_v<T>) { 
                    using Data = Self; //detail::remove_ref_or_ptr_t<T>;
                    Data * p_object = detail::is_sbo_eligible_with<Data>(sbo.size, sbo.alignment) ?
                        new(buffer) Data( static_cast<Data const&>(self()) )
                        :
                        new Data( static_cast<Data const&>(self()) );

                    auto * p_impl { static_cast<impl<Trait, T> *>( extra ) };
                    new(p_impl) impl<Trait,T> (p_object);
                } else if constexpr (std::is_object_v<T>) { 
                    if (detail::is_sbo_eligible_with<T>(sbo.size, sbo.alignment)) {
                        return new(buffer) impl<Trait,T>(self_);
                    } 
                    return new impl<Trait,T>(self_);
                }
            } break;

            ///@note move-operation for some<>
            case move_into: 
            if constexpr (vx::rvalue<T&&> && requires { impl<Trait,T>(std::move(self_)); }) {
                if constexpr (noexcept(impl<Trait,T>(std::move(self_)))) { 
                    // if (sizeof(T) <= sbo.size && alignof(T) <= sbo.alignment) {
                    if (detail::is_sbo_eligible_with<T>(sbo.size, sbo.alignment)) {
                        return new(buffer) impl<Trait,T>(std::move(self_));
                    }
                }
                return new impl<Trait, T>(std::move(self_)); 
            } break;

            ///@note the non-SBO case will be efficiently handled w/o the vcall
            case fsome_move_sbo_into: 
            if constexpr (std::is_pointer_v<T> && std::is_move_constructible_v<Self>) {
                using Data = Self; //detail::remove_ref_or_ptr_t<T>;
                Data * p_object = detail::is_sbo_eligible_with<Self>(sbo.size, sbo.alignment) ?
                    new(buffer) Self( std::move(self()) ) // fits into new SBO buffer => in-place move construct
                    :
                    new Self( std::move(self()) ); // else, allocate memory for it on the heap
                
                auto * p_impl { static_cast<impl<Trait, T> *>( extra ) };
                new(p_impl) impl<Trait,T> (p_object);
            } break;

            #if not VX_FSOME_ELIDE_VCALL_ON_MOVE
            case fsome_move_ptr_into: if constexpr (std::is_pointer_v<T>) {
                new(extra) impl<Trait, T> (std::exchange(self_, nullptr));
            } break;
            #endif

            //!@note: used exclusively in fsome
            case cleanup: [[unlikely]] {
                if constexpr (std::is_pointer_v<value_type>) {
                    using Data = std::remove_pointer_t<value_type>;
                    if (buffer != self_) {
                        delete static_cast<value_type>(self_);
                    } else {
                        static_cast<value_type>(self_)->~Data();
                    }
                }
            } break;
        }
        return nullptr;
    }
};

template <class CRTP, typename Trait>
struct basic_operations_for {
    auto* operator-> () noexcept { return iface(); }
    const auto* operator-> () const noexcept { return iface(); }
    Trait& operator*() const { return *iface(); }

    template <typename Target>
    Target* try_get() {
        auto * impl = dynamic_cast<CRTP::template impl_type<Target>*>(iface());
        return impl ? &impl->self() : nullptr;
    }

    template <typename Index>
    decltype(auto) operator[] (Index&& index) requires requires(Trait & t){ t[std::forward<Index>(index)]; }
    {
        return (*iface())[std::forward<Index>(index)];
    }

    decltype(auto) operator! () requires requires(Trait & t){ !t; }
    {
        return !(*iface());
    }

    template <typename... Ts>
    decltype(auto) operator()(Ts&&... args) requires requires(Trait && t){ t(std::forward<Ts>(args)...); }
    {
        return (*iface())(std::forward<Ts>(args)...);
    }

protected:
    auto* iface() { return static_cast<CRTP&>(*this).trait_ptr(); }
    const auto* iface() const { return static_cast<CRTP const&>(*this).trait_ptr(); }
};

template <class CRTP, typename>
struct multitrait_support_for {};

template <class CRTP, typename... Traits>
struct multitrait_support_for<CRTP, vx::mix<Traits...>> {};

struct empty_some_ptr_access : std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <typename Trait, std::size_t SBO_capacity, std::size_t alignment>
struct storage_for {
    using main_trait_t = first_trait_from<Trait>;

    template <typename X>
    static constexpr bool is_sbo_eligible = detail::is_sbo_eligible_with<X>(SBO_capacity, alignment);

    storage_for() = default;

    template <typename T>
    explicit storage_for(T&& object) {
        using impl_type = vx::impl< Trait, std::decay_t<T> >;
        if constexpr (is_sbo_eligible<impl_type>) { 
            p_trait = new(&buffer) impl_type(std::forward<T>(object));
        } else {
            p_trait = new impl_type(std::forward<T>(object));
        }
    }


    ~storage_for() noexcept {
        clear();
    }


    inline void clear() {
        if (not p_trait) { return; }
        if (this->stored_in_sbo()) {
            p_trait->~main_trait_t();
        } else {
            delete p_trait;
        }
    }
    
    template <typename T>
    inline void set(T&& data) {
        using impl_type = vx::impl< Trait, std::decay_t<T> >;
        if constexpr (is_sbo_eligible<impl_type>) { 
            /// [sbo] created in-place in SBO buffer
            p_trait = new(&buffer) impl_type(std::forward<T>(data));
        } else {
            /// [ptr] allocated and assigned to ptr
            p_trait = new impl_type(std::forward<T>(data));
        }
    }

    template <std::size_t dest_SBO, std::size_t dest_alignment>
    void copy_into(storage_for<Trait, dest_SBO, dest_alignment> & dest) const {
        dest.p_trait = (main_trait_t*)p_trait->do_action(detail::opcode::copy_into, (void*)&dest, {dest_SBO, dest_alignment});
    }

    template <std::size_t dest_SBO, std::size_t dest_alignment>
    void move_into(storage_for<Trait, dest_SBO, dest_alignment> & dest) && noexcept {
        if (this->stored_in_sbo()) {
            dest.p_trait = (main_trait_t*)p_trait->do_action(detail::opcode::move_into, (void*)&dest.buffer, {dest_SBO, dest_alignment});
        } else {
            dest.p_trait = std::exchange(p_trait, nullptr);
        }
    }


    bool stored_in_sbo() {
        return (void*)p_trait == (void*)&buffer;
    }


    alignas(alignment) std::byte buffer[SBO_capacity];
    main_trait_t * p_trait = nullptr;
};

template <typename Trait, std::size_t Alignment>
struct storage_for<Trait, 0, Alignment> {
    using main_trait_t = first_trait_from<Trait>;
    main_trait_t *p_trait = nullptr;

    template <typename X>
    static constexpr bool is_sbo_eligible = false;

    storage_for() = default;

    template <typename T>
    explicit storage_for(T&& object) {
        using impl_type = vx::impl< Trait, std::decay_t<T> >;
        p_trait = new impl_type(std::forward<T>(object));
    }

    ~storage_for() {
        clear();
    }

    inline void clear() { 
        if (p_trait) delete p_trait; 
    }
    
    template <typename T>
    inline void set(T&& data) {
        using impl_type = vx::impl< Trait, std::decay_t<T> >;
        p_trait = new impl_type(std::forward<T>(data));
    }

    template <std::size_t dest_SBO, std::size_t dest_alignment>
    void copy_into(storage_for<Trait, dest_SBO, dest_alignment> & dest) const {
        dest.p_trait = (main_trait_t*)p_trait->do_action(detail::opcode::copy_into, (void*)&dest.buffer, {dest_SBO, dest_alignment});
    }

    template <std::size_t dest_SBO, std::size_t dest_alignment>
    void move_into(storage_for<Trait, dest_SBO, dest_alignment> & dest) && noexcept {
        dest.p_trait = std::exchange(p_trait, nullptr);
    }
};

template <typename Trait=vx::trait, cfg::some config=cfg::some{}>
struct some : basic_operations_for<some<Trait, config>, Trait>,
              multitrait_support_for<some<Trait, config>, Trait>
{
    template <typename X> 
    using impl_type = vx::impl< Trait, std::remove_cvref_t<X> >;

    template <typename T2, cfg::some c2>
    friend struct some;
           
    some() requires(config.empty_state) =default;

    template <typename T>
    requires (not polymorphic<T>)
    some (T && obj) requires ((not config.copy || std::is_copy_constructible_v<std::remove_cvref_t<T>>)
                             &&
                             (not config.move || std::is_move_constructible_v<std::remove_cvref_t<T>>))
    : storage{std::forward<T>(obj)} {}
    
    ~some() = default;
    
    some(some const& other) {
        other.storage.copy_into(this->storage);
    }

    some& operator= (some const& other) {
        storage.clear();
        other.storage.copy_into(this->storage);
        return *this;
    }

    template <cfg::some config2>
    some(some<Trait, config2> const& other) {
        other.storage.copy_into(this->storage);
    }

    template <cfg::some config2>
    some& operator= (some<Trait, config2> const& other) {
        storage.clear();
        other.storage.copy_into(this->storage);
        return *this;
    }
    
    template <cfg::some config2>
    some(some<Trait, config2> && other) noexcept {
        std::move(other).storage.move_into(this->storage);
    }

    template <cfg::some config2>
    some& operator= (some<Trait, config2> && other) noexcept {
        storage.clear();
        std::move(other).storage.move_into(this->storage);
        return *this;
    }
    
protected:
    friend struct basic_operations_for<some<Trait, config>, Trait>;
    friend struct multitrait_support_for<some<Trait, config>, Trait>;

    const auto* trait_ptr() const { return storage.p_trait; }
    auto* trait_ptr() { return storage.p_trait; }
        
private:
    storage_for<Trait, config.sbo.size, config.sbo.alignment> storage;
};

} // namespace vx

#include <random>
/// A classic approach
struct IShape {
    virtual ~IShape() = default;
    virtual unsigned sides() const noexcept = 0;
    virtual void bump() noexcept = 0;
};
struct VSquare final : public IShape {
    int side_ = 0;
    unsigned sides() const noexcept override { return 4; }
    void bump() noexcept override { side_ += 1; }
};
struct VCircle final : public IShape {
    int radius_ = 0;
    unsigned sides() const noexcept override { return std::numeric_limits<unsigned>::max(); }
    void bump() noexcept override { radius_ += 1; }
};
/// Dynamic polymorphism
struct Shape : vx::trait {
    virtual unsigned sides() const noexcept = 0;
    virtual void bump() noexcept = 0;
};
struct Square { // no inheritance
    int side_ = 0;
    unsigned sides() const noexcept { return 4; }
    void bump() noexcept { side_ += 1; }
};
struct Circle {
    int radius_ = 0;
    unsigned sides() const noexcept { return std::numeric_limits<unsigned>::max(); }
    void bump() noexcept { radius_ += 1; }
};
/// impl for dynamic polymorphism
template <typename T>
struct vx::impl<Shape, T> final : impl_for<Shape, T> {
    using impl_for<Shape, T>::impl_for; // pull in the ctors
    using impl_for<Shape, T>::self;
    unsigned sides() const noexcept override { return self().sides(); }
    void bump() noexcept override { self().bump(); }
};

static constexpr std::size_t N = 1'000'000;

static void iterate_and_call_classic(benchmark::State& state) {
    std::vector<std::unique_ptr<IShape>> shapes;
    shapes.reserve(N);
    std::mt19937 mt{}; // default initialized for all tests
    for (std::size_t i = 0; i < N; ++i) {
        if (mt() % 2 == 0) {
            shapes.push_back(std::make_unique<VCircle>());
        } else {
            shapes.push_back(std::make_unique<VSquare>());
        }
    }
    // Testing the access times when iterating throught the vector
    for (auto _ : state) {
        std::size_t sides = 0;
        for (auto && p_shape : shapes) {
            sides += p_shape->sides();
        }
        benchmark::DoNotOptimize(sides);
    }
}

static void iterate_and_call_some(benchmark::State& state) {
    std::vector<vx::some<Shape>> shapes;
    shapes.reserve(N);
    // std::random_device rd;
    std::mt19937 mt {};
    for (std::size_t i = 0; i < N; ++i) {
        if (mt() % 2 == 0) {
            shapes.emplace_back(Circle{});
        } else {
            shapes.emplace_back(Square{});
        }
    }
    for (auto _ : state) {
        std::size_t sides = 0;
        for (auto && shape : shapes) {
            sides += shape->sides();
        }
        benchmark::DoNotOptimize(sides);
    }
}

static void iterate_and_call_some_no_sbo(benchmark::State& state) {
    std::vector<vx::some<Shape, vx::cfg::some{.sbo{0}}>> shapes;
    shapes.reserve(N);
    // std::random_device rd;
    std::mt19937 mt {};
    for (std::size_t i = 0; i < N; ++i) {
        if (mt() % 2 == 0) {
            shapes.emplace_back(Circle{});
        } else {
            shapes.emplace_back(Square{});
        }
    }
    for (auto _ : state) {
        std::size_t sides = 0;
        for (auto && shape : shapes) {
            sides += shape->sides();
        }
        benchmark::DoNotOptimize(sides);
    }
}

BENCHMARK(iterate_and_call_classic);
BENCHMARK(iterate_and_call_some);
BENCHMARK(iterate_and_call_some_no_sbo);