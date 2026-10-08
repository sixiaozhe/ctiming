#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace demo {

volatile double g_sink = 0.0;

double busy_kernel(double x, int iters) {
  double s = 0.0;
  for (int i = 0; i < iters; ++i) s += std::sin(x + i * 0.001) * std::cos(x - i * 0.002);
  g_sink += s;
  return s;
}

double light_leaf(int n) {
  double s = 0.0;
  for (int i = 0; i < n; ++i) s += i * 0.25;
  return s;
}

long fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

bool is_odd(int n);
bool is_even(int n) { return n == 0 ? true : is_odd(n - 1); }
bool is_odd(int n) { return n == 0 ? false : is_even(n - 1); }

void simulated_io(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

template <typename T>
T reduce_sum(const std::vector<T> &v) {
  T s = T{};
  for (const auto &x : v) s += x;
  return s;
}

struct Stage {
  std::string name;
  explicit Stage(std::string n) : name(std::move(n)) {}
  virtual ~Stage() = default;
  virtual double apply(double x, int iters) const = 0;
};

struct KernelStage : Stage {
  explicit KernelStage(std::string n) : Stage(std::move(n)) {}
  double apply(double x, int iters) const override { return busy_kernel(x, iters); }
};

struct ReduceStage : Stage {
  explicit ReduceStage(std::string n) : Stage(std::move(n)) {}
  double apply(double x, int iters) const override {
    std::vector<double> v(iters);
    for (int i = 0; i < iters; ++i) v[i] = x + i;
    return reduce_sum(v) + light_leaf(iters);
  }
};

struct Pipeline {
  std::string name;
  std::vector<const Stage *> stages;
  explicit Pipeline(std::string n) : name(std::move(n)) {}
  double run(double x, int iters) const {
    double acc = x;
    for (const Stage *s : stages) acc += s->apply(acc, iters);
    return acc;
  }
};

void worker(int seed, int iters) {
  Pipeline p("worker-pipeline");
  KernelStage k("kernel");
  ReduceStage r("reduce");
  p.stages.push_back(&k);
  p.stages.push_back(&r);
  double acc = p.run(seed * 0.5, iters);
  acc += static_cast<double>(fib(10 + (seed % 6)));
  acc += is_even(20 + seed) ? 1.0 : 0.0;
  g_sink += acc;
}

} // namespace demo

int main() {
  using namespace demo;
  const int iters = 4000;

  Pipeline main_pipe("main-pipeline");
  KernelStage main_kernel("main-kernel");
  ReduceStage main_reduce("main-reduce");
  main_pipe.stages.push_back(&main_kernel);
  main_pipe.stages.push_back(&main_reduce);
  double total = main_pipe.run(1.0, iters);

  total += static_cast<double>(fib(19));
  total += is_even(30) ? 2.0 : 0.0;
  total += is_odd(29) ? 3.0 : 0.0;

  simulated_io(5);

  std::vector<std::thread> pool;
  for (int t = 0; t < 4; ++t) pool.emplace_back(worker, t + 1, 1500);
  for (auto &th : pool) th.join();

  std::printf("done: %f\n", total);
  return 0;
}
