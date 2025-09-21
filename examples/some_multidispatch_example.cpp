#include <iostream>
#include <cassert>
#include "../some.hpp"

template <typename Trait>
using some 
    // = vx::some<Trait>;
    = vx::fsome<Trait>;

struct Addable : vx::trait {
    virtual ::some<Addable> add (::some<Addable>) = 0;
};

template <typename T>
struct vx::impl<Addable, T> : vx::impl_for<Addable, T> {
    using vx::impl_for<Addable, T>::impl_for;
    using vx::impl_for<Addable, T>::self;
    using Self = typename vx::impl_for<Addable, T>::Self;

    ::some<Addable> add (::some<Addable> other) override {
        if (auto b = other.try_get<Self>(); b) {
            return {self() + *b};
        } else {
            throw std::runtime_error {"different types cannot be summed in this simple example"};
        }
    }
};

some<Addable> operator+ (some<Addable> a, some<Addable> b) {
    return a->add(b);
}

int main() {
    some<Addable> a = 7;
    some<Addable> b = 3;
    // vx::some<Addable> i2 = Int{3};
    auto r = a + 3; //a->add(3);
    auto r2 = a + b; //or a->add(b);
    assert(( *r.try_get<int>() == *r2.try_get<int>() ));
    assert(( *r.try_get<int>() == 10 ));
    std::cerr << *r.try_get<int>();

    some<Addable> c = 3.f;
    auto r3 = c + .14f; // or c->add(.14f);
    std::cerr << *r3.try_get<float>();
}