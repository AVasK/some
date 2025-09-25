#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include "../some.hpp"

unsigned count_created = 0;
unsigned count_destroyed = 0;

struct Object {
    int x = 0;
    std::array<int, 100> arr;

    Object(int n) : x{n} { for (int i=0; i < 100; ++i) arr[i] = n; }
    Object() = default;

    // Object() { std::cerr << "Object()\n"; }
    // Object(Object&& other) 
    // : x{other.x}
    // , px{std::move(other.px)}
    // { std::cerr << "Object(Object&&)\n"; }
    // Object(Object const& other) 
    // : x{other.x}
    // , px{std::make_unique<int>(*other.px)}
    // { std::cerr << "Object(Object const&)\n"; }
    // ~Object() { std::cerr << "~Object\n"; }

    int number() const noexcept { return x; } // return 42; }
    void test() const noexcept { assert(( arr[99] == x )); }
    int mut() { 
        return x++; 
    }
};

struct TestInterface : vx::trait {
    virtual int number() const noexcept = 0;
    virtual void test() const noexcept = 0;
    virtual int mut() = 0;
};


template <typename T>
struct vx::impl<TestInterface, T> : vx::impl_for<TestInterface, T> { //: TestInterface, Self<T> {
    // using Self<T>::Self;
    // using Self<T>::self;

    using impl_for<TestInterface, T>::impl_for;
    using impl_for<TestInterface, T>::self;

    int number() const noexcept override { return self().number(); }
    virtual void test() const noexcept override { self().test(); }
    int mut() override { 
        return self().mut();
        // return vx::poly {this}->mut();
    }
};

struct FooBar {
    void foo() const { std::cerr << "Foo\n"; }
    void bar() const { std::cerr << "Bar\n"; }
};

struct Fooable : vx::trait {
    virtual void foo() const = 0;
};

template <typename T>
struct vx::impl<Fooable, T> : vx::impl_for<Fooable, T> {
    using impl_for<Fooable, T>::impl_for;

    void foo() const override { 
        vx::poly {this}->foo(); 
    }
};

struct Barable : vx::trait {
    virtual void bar() const = 0;
};

struct FooBarable : Fooable {
    virtual void bar() const = 0;
};


template <typename T>
struct vx::impl<Barable, T> : vx::impl_for<Barable, T> {
    using vx::impl_for<Barable, T>::impl_for;
    virtual void bar() const override { vx::poly{this}->bar(); }
};

template <typename T>
struct vx::impl<FooBarable, T> : vx::impl_for<FooBarable,T> {
    using vx::impl_for<FooBarable,T>::impl_for;
    // using vx::impl_for<FooBarable,T>::self;
    virtual void foo() const override { this->self().foo(); }
    virtual void bar() const override { vx::poly {this}->bar(); }
};

// template <typename T>
// struct vx::impl<Shape, T> : vx::impl_for<Shape, T> {
//     using impl_for<Shape, T>::impl_for;
// };

// template <typename T>
// struct vx::impl<TestInterface, T> : vx::impl_for<TestInterface, T> {
//     using vx::impl_for<TestInterface,T>::impl_for;
//     using vx::impl_for<TestInterface,T>::self;

//     int number() const noexcept override { 
//         // return self.number();          // one way to do it
//         return vx::poly {this}->number(); //another option
//     }

//     int mut() override { return self.mut(); }
// };

// or even
// template <typename T>
// struct vx::impl<TestInterface, T> : vx::impl_for<TestInterface, T> {
//     using vx::impl_for<TestInterface,T>::impl_for;

//     int number() const noexcept override { return vx::poly {this}->number(); }
//     int mut() override { return vx::poly{this}->mut(); }
// };


/// Typical inheritance-based polymorphism:

struct VTestInterface {
    ~VTestInterface()=default;
    virtual void test() const noexcept = 0;
    virtual int number() const noexcept = 0;
    virtual int mut() = 0;
};

struct VObject : VTestInterface {
    int x = 42;
    std::array<int, 100> arr {};
    virtual void test() const noexcept override { assert(( arr[99] == 0 )); }

    virtual int number() const noexcept override { return 42; }
    virtual int mut() override { return x++; }
};

/// Functions working with the polymorphic data:
///     Inheritance-based "standard" polymorphism
void read(VTestInterface const& c) {
    assert(( c.number() == 42 ));
    c.test();
    // c.mut(); // should not compile
}

void read(VTestInterface& c) {
    assert(( c.number() == 42 ));
    assert(( c.mut() == 42 )); // OK: non-const ref
    assert(( c.mut() == 43 ));
}

void read(vx::some<TestInterface const&> c) {
    std::cerr << "Reading a &\n";
    assert(( c->number() == 42 ));
    // c->mut(); // const ref => will not compile
}

void read_object(vx::some<TestInterface> & c) {
    std::cerr << "Object modifying read\n";
    assert(( c->number() == 42 ));
    assert(( c->mut() >= 0 ));
}

void read_object(vx::some<TestInterface> const& c) {
    std::cerr << "Reading an object by const&\n";
    assert(( c->number() == 42 ));
    // c->mut(); // should not compile
}


/// Shape example 
struct Triangle {
    Triangle() { std::cerr << "Triangle\n"; }
    ~Triangle() { std::cerr << "~Triangle\n"; }
};

struct Square {
    Square() { std::cerr << "Square\n"; }
    ~Square() { std::cerr << "~Square\n"; }
};

void shape_sink(vx::some<> shape) {
    if (shape.try_get<Square>()) {
        std::cerr << "got a Square\n";
    } else if (shape.try_get<Triangle>()) {
        std::cerr << "got a Triangle\n";
    } else {
        std::cerr << "got something else\n";
        assert(false);
    }
}

int main() {
    // Class-based poly
    {
        const VObject c{};
        VObject v{};

        read(c);
        read(v);
        read(VObject{});
    }

    // Some<> poly
    {
        const Object c{42};
        Object v{42};

        read(c); // takes a const 
        read(v); // takes a non-const

        [[maybe_unused]] vx::some<TestInterface> o {v};
        read_object( c );
        read_object( v );
        read_object( o );
    }
        // std::cerr << "Passing a temporary\n";
        // modify(Object{7}); // takes a temporary // MATERIALIZES A TEMP ON STACK OF A FUNCTION FRAME?
    {
        const Object c{7};
        Object v{8};
        Object v2{9};

        vx::some<TestInterface const&> sr = c;
        vx::some<TestInterface&> sr2 = v;
        
        vx::fsome<TestInterface> fs = v;
        assert(( fs->number() == v.number() ));

        vx::some_ptr<TestInterface> sp = &v;
        sp = &v2;
        // sp->mut(); // OK
        vx::some_ptr<const TestInterface> spcv = &v; 
        // spcv->mut(); // ERROR
        vx::some_ptr<const TestInterface> spc = &c;
        // spc->mut(); // ERROR
        const vx::some_ptr<TestInterface> cspv = &v;
        // cspv->mut(); // ERROR
        const vx::some_ptr<const TestInterface> cspc = &c;
        // cspc->mut(); // ERROR

        vx::poly_view<const TestInterface> pvc = c;
        // pvc->mut();

        vx::poly_view<const TestInterface> cpvc = c;
        vx::poly_view<const TestInterface> cpvv = v;
        // pvc2->mut();

        static_assert(std::is_constructible_v< vx::some_ptr<const TestInterface>, const Object* >);
        static_assert(std::is_constructible_v< vx::some_ptr<const TestInterface>, Object* >);
        static_assert(std::is_constructible_v< vx::some_ptr<TestInterface>, Object* >);
        static_assert(not std::is_constructible_v< vx::some_ptr<TestInterface>, const Object* >);

        static_assert(std::is_constructible_v< vx::poly_view<const TestInterface>, const Object& >);
        static_assert(std::is_constructible_v< vx::poly_view<const TestInterface>, Object& >);
        static_assert(std::is_constructible_v< vx::poly_view<TestInterface>, Object& >);
        static_assert(not std::is_constructible_v< vx::poly_view<TestInterface>, const Object& >);

        const vx::some_ptr<TestInterface> csp = &v;
        assert(( sp->number() == v2.number() ));
        assert(( sp->number() != sr2->number() ));
        assert(( csp->number() == sr2->number() ));
        // vx::some<TestInterface &> sr3 = Object{}; // DOESN'T GET ITS LIFETIME EXTENDED, THUS FORBIDDEN!
    }

    // Mixing multiple traits:
    {
        FooBar fb{};
        
        vx::some<Fooable&> s_foo = fb;
        std::cerr << sizeof(vx::some<Fooable&>) << '\n';
        s_foo->foo();
        assert(s_foo.try_get<FooBar>() != nullptr);

        vx::some<Barable&> s_bar = fb;
        std::cerr << sizeof(vx::some<Barable&>) << '\n';
        s_bar->bar();

        vx::some<FooBarable&> s_foobar = fb;
        std::cerr << sizeof(vx::some<FooBarable&>) << '\n';
        s_foobar->foo();
        s_foobar->bar();

        // vx::some<vx::mix<Fooable,Barable>&> s_foobar2 = fb;
        vx::some<vx::mix<Fooable,Barable>> s_foobar2 = fb;
        std::cerr << sizeof(vx::some<vx::mix<Fooable,Barable>&>) << '\n';
        s_foobar2->foo();
        s_foobar2.as<Barable>()->bar();
        assert( s_foobar2.try_get<FooBar>() != nullptr );

        vx::some<vx::mix<Fooable,Barable>> s_foobar2v = fb;
        assert( s_foobar2v.try_get<FooBar>() != nullptr );
        // s_foobar2.as<TestInterface>();
    }

    // Shapes and dtors:
    shape_sink(Triangle{});
    shape_sink(Square{});

    // Test copies and moves:
    {
        count_destroyed = 0;
        count_created = 0;
        // uses global created / destroyed counts
        struct Verbose {
            long number = 111777888000;
            int version = 0;
            Verbose(){ std::cerr << "Verbose::Ctor\n"; count_created += 1; }
            ~Verbose(){ std::cerr << "Verbose::Dtor" << version << "\n"; if (version != -1) count_destroyed += 1; }
            Verbose(Verbose const& other) : version{other.version + 1} { std::cerr << "Verbose::Copy: " << version << "\n"; count_created += 1; }
            Verbose(Verbose && other) noexcept : version{std::exchange(other.version, -1)}{ std::cerr << "Verbose::Move\n"; }
        };

        vx::some<> x {Verbose{}};
        vx::some<> y {x}; // Copy 1
        assert(x.try_get<Verbose>()->number == y.try_get<Verbose>()->number);

        vx::some<vx::trait, vx::cfg::some{.sbo{0, 0}}> y2 {y}; // Copy 2

        static_assert( sizeof(vx::some<vx::trait, vx::cfg::some{.sbo{0, 0}}>) == sizeof(void*) );
        // static_assert( sizeof(vx::some<>) == sizeof(void*)+ vx::cfg::some{}.sbo.size );
        vx::some<> y3 {}; // Copy 3
        y3 = y2;
        y3 = Verbose{};
        Verbose v{};
        y3 = v;
        y3 = std::move(v);
        vx::some<> z {std::move(x)}; // Version 0
        assert(y.try_get<Verbose>()->number == z.try_get<Verbose>()->number);
        vx::some<> z2 {};
        z2 = z;
        z2 = std::move(z);
        assert(y.try_get<Verbose>()->number == y2.try_get<Verbose>()->number);
        assert(y.try_get<Verbose>()->number == y3.try_get<Verbose>()->number);
    }
    assert(count_created == count_destroyed);

    /// DITTO for the fsome:
     // Test copies and moves:
    {
        count_destroyed = 0;
        count_created = 0;
        // uses global created / destroyed counts
        struct Verbose {
            long number = 111777888000;
            int version = 0;
            Verbose(int v = 0) : version{v} { std::cerr << "Verbose::Ctor " << v << "\n"; count_created += 1; }
            ~Verbose(){ std::cerr << "Verbose::Dtor" << version << "\n"; if (version != -1) count_destroyed += 1; }
            Verbose(Verbose const& other) : version{other.version + 1} { std::cerr << "Verbose::Copy: " << version << "\n"; count_created += 1; }
            Verbose(Verbose && other) noexcept : version{std::exchange(other.version, -1)}{ std::cerr << "Verbose::Move\n"; }
        };

        vx::fsome<> x {Verbose{7}};
        vx::fsome<> y {x}; // Copy 1
        assert(x.try_get<Verbose>()->number == y.try_get<Verbose>()->number);

        vx::fsome<vx::trait, vx::cfg::fsome{.sbo{0}}> ex {Verbose{14}};
        vx::fsome<vx::trait, vx::cfg::fsome{.sbo{0}}> mex { std::move(ex) };
        vx::fsome<vx::trait, vx::cfg::fsome{.sbo{0}}> y2 {y}; // Copy 2

        static_assert( sizeof(vx::fsome<vx::trait, vx::cfg::fsome{.sbo{0, 0}}>) == 2*sizeof(void*) );
        // static_assert( sizeof(vx::some<>) == sizeof(void*)+ vx::cfg::some{}.sbo.size );
        {
            vx::fsome<vx::trait, vx::cfg::fsome{.sbo{24}}> e {};
            vx::fsome<vx::trait, vx::cfg::fsome{.sbo{24}}> cpy_e {e};
            vx::fsome<vx::trait, vx::cfg::fsome{.sbo{24}}> mov_e {std::move(e)};
        }
        {
            vx::fsome<vx::trait, vx::cfg::fsome{.sbo{0}}> e {};
            vx::fsome<vx::trait, vx::cfg::fsome{.sbo{0}}> cpy_e {e};
            vx::fsome<vx::trait, vx::cfg::fsome{.sbo{0}}> mov_e {std::move(e)};
        }

        vx::fsome<> z {std::move(x)}; // Version 0
        assert(y.try_get<Verbose>()->number == z.try_get<Verbose>()->number);
        vx::fsome<> z2 {};
        z2 = z;
        z2 = Verbose{};
        Verbose v{};
        z2 = v;
        z2 = std::move(v);
        z2 = std::move(z);
        assert(y.try_get<Verbose>()->number == y2.try_get<Verbose>()->number);
        // assert(y.try_get<Verbose>()->number == y3.try_get<Verbose>()->number);
    }
    assert(count_created == count_destroyed);


    /// SOME2: Fat ptr impl of some:
    {
        struct Foo {
            void foo() const { std::cerr << "foo\n"; }
            ~Foo() { std::cerr << "~Foo\n"; }
        };

        vx::fsome<Fooable, vx::cfg::fsome{.sbo{0}}> f = Foo{};
        f->foo();
        f.try_get<Foo>()->foo();
        vx::fsome<Fooable> f2 = Foo{};
        f2->foo();

        static_assert(sizeof(vx::fsome<Fooable, vx::cfg::fsome{.sbo{0}}>) == 2*sizeof(void*));
    }

    {
        // testing with different possible types / configs:
        struct noncopyable {
            noncopyable() = default;
            noncopyable(noncopyable const&) = delete;
            noncopyable(noncopyable &&) = default; // but movable
        };

        struct nonmovable {
            nonmovable() = default;
            nonmovable(nonmovable const&) = default; // but copyable
            nonmovable(nonmovable &&) = delete;
        };

        struct inplace {
            inplace() = default;
            inplace(inplace const&) = delete;
            inplace(inplace &&) = delete;
        };

        const auto nm = nonmovable();

        /// ===== some:
        [[maybe_unused]] vx::some<vx::trait, vx::cfg::some{.copy=false, .move=false}> sc{noncopyable()};
        [[maybe_unused]] vx::some<vx::trait, vx::cfg::some{.copy=false, .move=false}> sm{nm};

        static_assert(std::is_default_constructible_v< vx::some<> >,
            "the .empty_state=true in default configuration");

        static_assert(not std::is_default_constructible_v< vx::some<vx::trait, vx::cfg::some{.empty_state=false}> >,
            "the .empty_state=false in the configuration means that some is not a nullable type, which means some optimizations are possible");

        static_assert(not std::is_constructible_v< vx::some<>, noncopyable>,
            "copyability is demanded in a default configuration for some<>");

        static_assert(std::is_constructible_v< vx::some<vx::trait, vx::cfg::some{.copy=false}>, noncopyable>,
            "copyability is not demanded in this configuration for some<>");

        static_assert(not std::is_constructible_v< vx::some<>, nonmovable>,
            "move constructor is mandatory since the default configurations sets .move=true");

        static_assert(std::is_constructible_v< vx::some<vx::trait, vx::cfg::some{.move=false}>, nonmovable>,
            "move constructor is not mandatory since the configurations sets .move=false");

        static_assert(not std::is_constructible_v< vx::some<>, inplace>,
            "at least copyability is demanded in a default configuration for some<>");

        static_assert(std::is_constructible_v< vx::some<vx::trait, vx::cfg::some{.copy=false, .move=false}>, inplace>);

        /// ===== fsome:
        [[maybe_unused]] vx::fsome<vx::trait, vx::cfg::fsome{.copy=false, .move=false}> fc{noncopyable()};
        [[maybe_unused]] vx::fsome<vx::trait, vx::cfg::fsome{.copy=false, .move=false}> fm{nm};

        static_assert(std::is_default_constructible_v< vx::fsome<> >,
            "the .empty_state=true in default configuration");

        static_assert(not std::is_default_constructible_v< vx::fsome<vx::trait, vx::cfg::fsome{.empty_state=false}> >,
            "the .empty_state=false in the configuration means that fsome is not a nullable type, which means some optimizations are possible");

        static_assert(not std::is_constructible_v< vx::fsome<>, noncopyable>,
            "copyability is demanded in a default configuration for fsome<>");

        static_assert(std::is_constructible_v< vx::fsome<vx::trait, vx::cfg::fsome{.copy=false}>, noncopyable>,
            "copyability is not demanded in this configuration for fsome<>");

        static_assert(not std::is_constructible_v< vx::fsome<>, nonmovable>,
            "move constructor is mandatory since the default configurations sets .move=true");

        static_assert(std::is_constructible_v< vx::fsome<vx::trait, vx::cfg::fsome{.move=false}>, nonmovable>,
            "move constructor is not mandatory since the configurations sets .move=false");

        static_assert(not std::is_constructible_v< vx::fsome<>, inplace>,
            "at least copyability is demanded in a default configuration for fsome<>");

        static_assert(std::is_constructible_v< vx::fsome<vx::trait, vx::cfg::fsome{.copy=false, .move=false}>, inplace>);   
    }

    /// Test casts:
    {
        const auto test_casts = [](auto &o, const auto& co) {
            static_assert(std::same_as< decltype(o.template try_get<int>()), int * >);
            static_assert(std::same_as< decltype(co.template try_get<int>()), const int * >);
            static_assert(std::same_as< decltype(co.template try_get<const int>()), const int * >);
            assert(*o.template try_get<int>() == 1); // -> int *
            assert(*co.template try_get<int>() == 1); // -> const int *
            assert(*co.template try_get<const int>() == 1); // -> const int *
            *o.template try_get<int>() = 2;
            // *co.try_get<int>() = 2; // ERROR

            // same for some_cast:
            static_assert(std::same_as< decltype(vx::some_cast<int>(&o)), int * >);
            static_assert(std::same_as< decltype(vx::some_cast<int>(&co)), const int * >);
            static_assert(std::same_as< decltype(vx::some_cast<const int>(&co)), const int * >);

            static_assert(std::same_as< decltype(vx::some_cast<int*>(o)), int * >);
            static_assert(std::same_as< decltype(vx::some_cast<const int*>(o)), const int *>);
            static_assert(std::same_as< decltype(vx::some_cast<int*>(co)), const int * >);
            static_assert(std::same_as< decltype(vx::some_cast<const int*>(co)), const int * >);

            static_assert(std::same_as< decltype(vx::some_cast<int>(o)), int>);
            static_assert(std::same_as< decltype(vx::some_cast<int>(co)), int>);
            static_assert(std::same_as< decltype(vx::some_cast<int&>(o)), int&>);
            static_assert(std::same_as< decltype(vx::some_cast<const int&>(o)), const int&>);
            static_assert(std::same_as< decltype(vx::some_cast<const int&>(co)), const int&>);

            static_assert(std::same_as< decltype(vx::some_cast<int>(std::move(o))), int>);
            static_assert(std::same_as< decltype(vx::some_cast<int>(std::move(co))), int>);
        };

        vx::some<> s = 1;
        const vx::some<> cs = 1;
        test_casts(s, cs);

        vx::fsome<> f = 1;
        const vx::fsome<> cf = 1;
        test_casts(f, cf);

        int i = 1;
        const int ci = 1;
        vx::some<vx::trait&> v = i;
        vx::some<vx::trait const&> cv = ci;
        test_casts(v, cv);
    }

    /// Test casts from examples
    {
        vx::some<> anything = 1;
        assert(( *anything.try_get<int>() == 1 ));
        assert(( vx::some_cast<int>(anything) == 1 ));
        vx::some_cast<int&>(anything) = 7;
        assert(( vx::some_cast<int>(anything) == 7 ));
        anything = std::string{"hi"};
        std::cout << vx::some_cast<std::string const&>(anything);
    }
    {
        vx::fsome<> anything = 1;
        assert(( *anything.try_get<int>() == 1 ));
        assert(( vx::some_cast<int>(anything) == 1 ));
        vx::some_cast<int&>(anything) = 7;
        assert(( vx::some_cast<int>(anything) == 7 ));
        anything = std::string{"hi"};
        std::cout << vx::some_cast<std::string const&>(anything);
    }
    
}