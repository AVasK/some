# `Some` is a runtime polymorphism library for modern `C++`, supporting `C++20` and onwards.

### What is it and what problems does it solve? 
This is a single-header, zero extrnal dependencies, and easy-to-use type-erasing runtime polymorphism library. 
But that sounds scary, so let's just say it's similar to the [Rust's Traits](https://doc.rust-lang.org/book/ch18-02-trait-objects.html) or [Go's interfaces](https://gobyexample.com/interfaces).
Or if we were to stay in the C++ land, it's somewhat similar to [Dyno](https://github.com/ldionne/dyno/), [Folly::Poly](https://github.com/facebook/folly/blob/0d868697d003578e42a8b5c445747b7a0bda4a49/folly/docs/Poly.md) or [AnyAny](https://github.com/kelbon/AnyAny/). However `some` was built with different trade-offs in mind and trying to steamline the user experience as much as possible without introducing any DSL-like descriptions and/or macros.

Here is a very simple example of how you can use some<> even without Traits (about them in a second)
```C++
// without a trait, some can be used as std::any, but with a bunch of extra tricks up its sleeve (configurable SBO, configurable copy and move, ...)
some<> anything = 1;
std::cout << *anything.try_get<int>();
```
Here is another simple example of how you would use it:
Let's say we have a simple struct Square that has a method  `void draw(std::ostream&)`:
```C++
struct Square { // notice there is no inheritance
    //... some members? 
    //... and plain functions
    void draw(std::ostream& out) const { out << "[ ]\n"; }
    unsigned sides() const { return 4; }
};
```
And for now let's just say that we already have a trait `Shape` that defines a method  `void draw(std::ostream&)`
```C++
vx::some<Shape> obj = Square{};
obj->draw(std::cout);
```

Yes, it's that easy. Note, however, when accessing the polymorphic methods we should use the arrow `->` syntax. 
Now, let's see how to define such a trait:
```C++
struct Shape : vx::trait {
    virtual void draw(std::ostream&) const = 0;
};

/// describe the implementation
template <typename T>
struct vx::impl<Shape, T> final : impl_for<Shape, T> {
    using impl_for<Shape, T>::impl_for; // pull in the ctors

    void draw(std::ostream& out) const override { // just as you would if a Square was derived from
        vx::poly {this}->draw(out);               // some IShape that had a virtual function for overriding
    }
};
```

Not too bad, is it? There still is some boilerplate, but it is only written once per trait and as a result you have all the benefits for free for any objects that happen to satisfy this interface.

<details>
<summary>
Benchmarks
</summary>
  
I decided to use [quick-bench](https://quick-bench.com) website for convenience, as the results (in theory) would be easy to assess. 
<details>
  
<summary> A little ranting on why it isn't as easy to assess as I'd hope </summary>
At least that was the plan, as it turns out the website clearly has a limit on code size, doesn't appear to support `#include` with github links and I couldn't find a way to create a permalink to the benchmarks I somehow managed to squeeze in there. Due to the awfully low limit on code size, I had to crop the fsome and some into parts and also re-format it in the ugliest way possible, but here we are...
</details>

All the plots and the related code live in the `quick_benchmark_examples` folder. Under every plot you'll see here will be a link to the full benchmark code that you can copy and paste into the [quick-bench](https://quick-bench.com) to experiment. 

#### Benchmarking `fsome` iterations:
```C++
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
```
![fsome_iterate_through_vector](quick_benchmark_examples/fsome_quick_bench.png)
[fsome_iterations_bench](quick_benchmark_examples/quick_bench_fsome.cpp)

#### Benchmarking `some` iterations:
```C++

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
```
![some_iterate_through_vector](quick_benchmark_examples/some_quick_bench.png)
[some_iterations_bench](quick_benchmark_examples/quick_bench_some.cpp)

#### Benchmarking `fsome` call and set to new object in a loop
```C++
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
```
![fsome_call_and_set](quick_benchmark_examples/fsome_call.png)
[fsome_call_and_set_bench](quick_benchmark_examples/quick_bench_fsome_call.cpp)

#### some call and set
![some_call_and_set](quick_benchmark_examples/some_call.png)
[some_call_and_set_bench](quick_benchmark_examples/quick_bench_some_call_measure.cpp)

</details>
