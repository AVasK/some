// Copyright (C) Alexander Vaskov 2025
#include <concepts>
#include <cstdint> //ints
#include <cstring> //memcpy
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
struct SBO { vx::u16 size { 0 }; vx::u16 alignment { alignof(std::max_align_t) }; };
struct fsome { SBO sbo {0}; bool copy {true}; bool move {true}; bool empty_state {false}; bool check_empty {VX_HARDENED}; };
}// namespace cfg
/// ===== [ FWD Declarations ] =====
template <class Trait, typename T> struct impl_for;
template <typename Trait, cfg::fsome> struct fsome;
namespace detail {
    template <typename> struct is_polymorphic : std::false_type {};
    template <typename Trait, cfg::fsome config> struct is_polymorphic< fsome<Trait, config> > : std::true_type {};
} // namespace detail

template <typename T> concept polymorphic = detail::is_polymorphic<std::remove_cvref_t<T>>::value;
namespace detail {
template <typename T> struct remove_innermost_const_impl { using type = T; };
template <typename T> struct remove_innermost_const_impl<T const&> { using type = T&; };
template <typename T> struct remove_innermost_const_impl<T const&&> { using type = T&&; };
template <typename T> struct remove_innermost_const_impl<T const*> { using type = T*; };
template <typename T> using remove_innermost_const = typename detail::remove_innermost_const_impl<T>::type;
template <typename Ptr> concept pointer_like = std::is_pointer_v<Ptr> || requires (Ptr p) {
    { *p };
    { static_cast<bool>(p) };
    { p.operator->() } -> std::convertible_to<decltype( &*p )>;
};
}//namespace detail
template <typename T> 
concept rvalue = std::is_rvalue_reference_v<T&&> && !std::is_const_v<T>;
/// ===== [ IMPL ] =====
template <class Trait, typename T>
struct impl;

namespace detail {
    template <typename T> using remove_ref_or_ptr_t = std::conditional_t<std::is_reference_v<T>, std::remove_reference_t<T>, std::conditional_t<std::is_pointer_v<T>, std::remove_pointer_t<T>, T> >;
    enum class opcode : vx::u8 { copy_into, move_into, fsome_move_sbo_into, cleanup };
}//namespace detail

namespace detail {
template <typename T>
constexpr bool is_sbo_eligible_with(u16 SBO_capacity, u16 SBO_alignment) { return sizeof(T) <= SBO_capacity && alignof(T) <= SBO_alignment && (SBO_alignment % alignof(T) == 0) && (not std::is_move_constructible_v<T> || std::is_nothrow_move_constructible_v<T>); }
}// namespace detail

struct trait {
    trait() = default;
    trait (trait const&) = delete;
    trait (trait &&) = delete;
    virtual ~trait()=default;
private:
    template <class Trait, typename T> friend struct impl_for;
    template <typename Trait, std::size_t, std::size_t> friend struct storage_for;
    template <typename Trait, cfg::fsome> friend struct fsome;
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
    impl_for(impl_for const&)=default;
    explicit impl_for(std::convertible_to<T> auto&& other) 
    noexcept(std::is_nothrow_constructible_v<value_type, decltype(other)>)
    : self_{std::forward<decltype(other)>(other)} {}

    const auto* operator->() const { return get(); }
    auto* operator->() { return get(); }

    auto* get() & { 
        if constexpr (std::is_pointer_v<value_type>) {
            return self_;
        } else {
            return &self_;
        }
    }

    const auto* get() const& {
        if constexpr (std::is_pointer_v<value_type>) {
            return self_;
        } else {
            return &self_;
        }
    }

    auto& self() { return *get(); }
    auto const& self() const { return *get(); }
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
                        new(buffer) Data( static_cast<Data const&>(self()) ) : new Data( static_cast<Data const&>(self()) );
                    auto * p_impl { static_cast<impl<Trait, T> *>( extra ) };
                    new(p_impl) impl<Trait,T> (p_object);
                } else if constexpr (std::is_object_v<T>) { 
                    if (detail::is_sbo_eligible_with<T>(sbo.size, sbo.alignment)) {
                        return new(buffer) impl<Trait,T>(self_);
                    } 
                    return new impl<Trait,T>(self_);
                }
            } break;
            case move_into: 
            if constexpr (vx::rvalue<T&&> && requires { impl<Trait,T>(std::move(self_)); }) {
                if constexpr (noexcept(impl<Trait,T>(std::move(self_)))) { 
                    if (detail::is_sbo_eligible_with<T>(sbo.size, sbo.alignment)) {
                        return new(buffer) impl<Trait,T>(std::move(self_));
                    }
                }
                return new impl<Trait, T>(std::move(self_)); 
            } break;
            case fsome_move_sbo_into: 
            if constexpr (std::is_pointer_v<T> && std::is_move_constructible_v<Self>) {
                using Data = Self; //detail::remove_ref_or_ptr_t<T>;
                Data * p_object = detail::is_sbo_eligible_with<Self>(sbo.size, sbo.alignment) ?
                    new(buffer) Self( std::move(self()) ) // fits into new SBO buffer => in-place move construct
                    : new Self( std::move(self()) ); // else, allocate memory for it on the heap
                
                auto * p_impl { static_cast<impl<Trait, T> *>( extra ) };
                new(p_impl) impl<Trait,T> (p_object);
            } break;

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
    { return (*iface())[std::forward<Index>(index)]; }

    decltype(auto) operator! () requires requires(Trait & t){ !t; }
    { return !(*iface()); }

    template <typename... Ts>
    decltype(auto) operator()(Ts&&... args) requires requires(Trait && t){ t(std::forward<Ts>(args)...); }
    { return (*iface())(std::forward<Ts>(args)...); }

protected:
    auto* iface() { return static_cast<CRTP&>(*this).trait_ptr(); }
    const auto* iface() const { return static_cast<CRTP const&>(*this).trait_ptr(); }
};

struct empty_some_ptr_access : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// ===== [ SOME PTR ] =====
template <class Trait, bool checked=false>
class some_ptr : public basic_operations_for<some_ptr<Trait>, std::remove_cv_t<Trait>> {
    friend struct basic_operations_for<some_ptr<Trait>, std::remove_cv_t<Trait>>;
    template <typename, cfg::fsome> friend struct fsome;
    using layout = struct { void* vptr; void* dptr; };
    static constexpr auto k_trait_size = sizeof(Trait) + sizeof(void*);
    static constexpr std::size_t alignment = alignof(Trait);
    using raw_trait_t = std::remove_cv_t<Trait>;
    template <typename X> using impl_type = impl<raw_trait_t, X*>;
public:
    template <typename T>
    requires( std::is_const_v<Trait> || not std::is_const_v<T> )
    void set(T * p) {
        static_assert(sizeof(impl<raw_trait_t, T*>) == k_trait_size);
        new(&iface) impl<raw_trait_t, std::remove_const_t<T>*>{const_cast<std::remove_const_t<T>*>(p)};
    }

    template <typename T>
    requires( std::is_const_v<Trait> || not std::is_const_v<T> )
    some_ptr(T * p) {
        set(p);
    }

    template <typename T, typename Del>
    requires( std::is_const_v<Trait> || not std::is_const_v<T> )
    some_ptr(std::unique_ptr<T,Del> p) {
        set(p.get());
        p.release();
    }

    some_ptr() { new(&iface) layout{nullptr, nullptr}; }

    some_ptr(std::nullptr_t) : some_ptr{} {}

    ~some_ptr() {
        VX_SOME_LOG(inspect().vptr << "|" << inspect().dptr);
        if (empty()) { return; }
        trait_ptr()->~raw_trait_t(); // in case impl<Trait, T*> has some special destructors
    }
    protected:
    void steal_trait_from(some_ptr & other) {
        std::memcpy(&iface, &other.iface, k_trait_size);
        new(&other.iface) layout{nullptr, nullptr};
    }

    inline layout inspect() const noexcept {
        return std::bit_cast<layout>(iface);
    }

    bool empty() const noexcept {
        return inspect().vptr == nullptr; 
    }

    void clear() noexcept(not checked) {
        if (empty()) { return; }
        trait_ptr()->~raw_trait_t(); // in case impl<Trait, T*> has some special destructors
    }

    std::add_pointer_t<const raw_trait_t> trait_ptr() const noexcept(not checked) {
        if constexpr (checked) {
            if (empty()) throw empty_some_ptr_access{"empty some_ptr accessed"};
        }
        return std::launder(reinterpret_cast<std::add_pointer_t<const raw_trait_t>>(&iface)); 
    }

    std::add_pointer_t<Trait> trait_ptr() noexcept(not checked) {
        if constexpr (checked) {
            if (empty()) throw empty_some_ptr_access{"empty some_ptr accessed"};
        }
        return std::launder(reinterpret_cast<std::add_pointer_t<Trait>>(&iface));
    }
    alignas(alignment) std::byte iface[k_trait_size] = {}; 
};

template <std::size_t capacity, std::size_t alignment>
struct fsome_storage_policy {

    template <typename X>
    static constexpr bool is_sbo_eligible = detail::is_sbo_eligible_with<X>(capacity, alignment);

    fsome_storage_policy() = default;

    void* get_sbo_buffer() noexcept { return &sbo[0]; }

    template <typename T, typename X = std::remove_cvref_t<T>>
    auto make(T && obj) {
        if constexpr (is_sbo_eligible<X>) {
            using Deleter = decltype([](X * p){ p->~X(); });
            return std::unique_ptr<X, Deleter>(new(&sbo) X(std::forward<T>(obj)));
        } else {
            return std::make_unique<X>(obj);
        }
    }
private:
    alignas(alignment) std::byte sbo[capacity];
};

template <std::size_t alignment>
struct fsome_storage_policy<0, alignment> {
    fsome_storage_policy() = default;
    constexpr void* get_sbo_buffer() const noexcept { return nullptr; }

    template <typename T, typename X = std::remove_cvref_t<T>>
    auto make(T && obj) { return std::make_unique<X>(std::forward<T>(obj)); }
};

template <typename Trait=vx::trait, vx::cfg::fsome config = vx::cfg::fsome{}>
struct fsome : public fsome_storage_policy<config.sbo.size, config.sbo.alignment>,
               public basic_operations_for<fsome<Trait, config>, Trait> {    
    template <typename, vx::cfg::fsome> friend struct fsome;
    template <typename X> using impl_type = some_ptr<Trait, config.check_empty>::template impl_type<X>;
    using storage_policy = fsome_storage_policy<config.sbo.size, config.sbo.alignment>;

    fsome() requires(config.empty_state) =default;

    template <typename T> fsome(T && obj) requires (not polymorphic<T>
                             &&
                             (not config.copy || std::is_copy_constructible_v<std::remove_cvref_t<T>>)
                             &&
                             (not config.move || std::is_move_constructible_v<std::remove_cvref_t<T>>))
    : poly_{ this->make(std::forward<T>(obj)) }
    {}
    
    fsome(fsome const& other) : poly_{} { other.copy_into(*this); }

    template <cfg::fsome other_config>
    fsome(fsome<Trait, other_config> const& other) : poly_{} { other.copy_into(*this); }

    template <cfg::fsome other_config>
    fsome& operator= (fsome<Trait, other_config> const& other) {
        if constexpr (other_config.empty_state) { if (other.poly_.empty()) { return *this; } }
        clear();
        other.copy_into(*this);
        return *this;
    }

    fsome& operator= (fsome const& other) {
        if constexpr (config.empty_state) { if (other.poly_.empty()) { return *this; } }
        clear();
        other.copy_into(*this);
        return *this;
    }
    
    fsome(fsome && other) noexcept : poly_{} {
        std::move(other).move_into(*this);
    }

    template <cfg::fsome other_config>
    fsome(fsome<Trait, other_config> && other) noexcept : poly_{} {
        std::move(other).move_into(*this);
    }

    template <cfg::fsome other_config>
    fsome& operator= (fsome<Trait, other_config> && other)
    {
        if constexpr (other_config.empty_state) { if (other.poly_.empty()) { return *this; } }
        clear();
        std::move(other).move_into(*this);
        return *this;
    }
    
    ~fsome() {
        if (not poly_.empty()) { poly_->do_action(detail::opcode::cleanup, this->get_sbo_buffer(), {0,0}); }
    }
protected:
    friend struct basic_operations_for<fsome<Trait, config>, Trait>;
    auto* trait_ptr() noexcept { return poly_.trait_ptr(); }
    auto const* trait_ptr() const noexcept { return poly_.trait_ptr(); }

    void clear() noexcept {
        if constexpr (config.empty_state) {
            if (poly_.empty()) { return; }
        }
        poly_->do_action(detail::opcode::cleanup, this->get_sbo_buffer(), config.sbo);
    }

    template <cfg::fsome other_config>
    void copy_into(fsome<Trait, other_config> & other) const& {
        if constexpr (config.empty_state) {
            if (this->poly_.empty()) { return; }
        }
        const_cast<fsome&>(*this)->do_action(
            detail::opcode::copy_into, other.get_sbo_buffer(), other_config.sbo, (void*)&other.poly_.iface);
    }

    template <cfg::fsome other_config>
    void move_into(fsome<Trait, other_config> & other) && noexcept
    {
        if constexpr (config.sbo.size == 0) { 
            other.poly_.steal_trait_from(this->poly_);
        } else {
            if (this->poly_.inspect().dptr == this->get_sbo_buffer()) {
                if constexpr (config.empty_state) {
                    if (poly_.empty()) { return; }
                }
                poly_->do_action(detail::opcode::fsome_move_sbo_into, 
                    other.get_sbo_buffer(), other_config.sbo, (void*)&other.poly_.iface);
            } else {
                other.poly_.steal_trait_from(this->poly_);
            }
        }
    }
private:
    some_ptr<Trait, config.check_empty> poly_{}; // {vptr + data_ptr} 
};
} // namespace vx

#include <random>
/// A classic approach
struct IShape {
    virtual ~IShape() = default;
    virtual int info() const noexcept = 0;
    virtual void bump() noexcept = 0;
};
struct VSquare final : public IShape {
    int side_ = 0;
    int info() const noexcept override { return side_; }
    void bump() noexcept override { side_ += 1; }
};
struct VCircle final : public IShape {
    int radius_ = 0;
    int info() const noexcept override { return radius_; }
    void bump() noexcept override { radius_ -= 1; }
};
/// Dynamic polymorphism
struct Shape : vx::trait {
    virtual int info() const noexcept = 0;
    virtual void bump() noexcept = 0;
};
struct Square { // no inheritance
    int side_ = 0;
    int info() const noexcept { return side_; }
    void bump() noexcept { side_ += 1; }
};
struct Circle {
    int radius_ = 0;
    int info() const noexcept { return radius_; }
    void bump() noexcept { radius_ -= 1; }
};
/// impl for dynamic polymorphism
template <typename T>
struct vx::impl<Shape, T> final : impl_for<Shape, T> {
    using impl_for<Shape, T>::impl_for; // pull in the ctors
    using impl_for<Shape, T>::self;
    int info() const noexcept override { return self().info(); }
    void bump() noexcept override { self().bump(); }
};


static_assert(vx::detail::is_sbo_eligible_with<Square>(24, 8));

static constexpr std::size_t N = 100'000;

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
            sides += p_shape->info();
            p_shape->bump();
        }
        benchmark::DoNotOptimize(sides);
    }
}

static void iterate_and_call_fsome(benchmark::State& state) {
    std::vector<vx::fsome<Shape>> shapes;
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
            sides += shape->info();
            shape->bump();
        }
        benchmark::DoNotOptimize(sides);
    }
}

static void iterate_and_call_fsome_sbo(benchmark::State& state) {
    std::vector<vx::fsome<Shape, vx::cfg::fsome{.sbo{16}}>> shapes;
    shapes.reserve(N);
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
            sides += shape->info();
            shape->bump();
        }
        benchmark::DoNotOptimize(sides);
    }
}

BENCHMARK(iterate_and_call_classic);
BENCHMARK(iterate_and_call_fsome);
BENCHMARK(iterate_and_call_fsome_sbo);