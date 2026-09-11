# Contributing to QuantDesk

Thank you for your interest in contributing to **QuantDesk**! We welcome institutional-grade contributions, performance optimizations, and bug fixes.

---

## 🛠️ Development Workflow

1. **Prerequisites**:
   - C++20 compliant compiler (`GCC 12+`, `Clang 15+`, or `MSVC 2022+`)
   - `CMake 3.20+`
   - Python 3.10+ (with `pytest` and `websockets`)

2. **Building the Project**:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```

3. **Running the Test Suite**:
   ```bash
   ctest --test-dir build --output-on-failure
   python -m pytest tests/
   ```

4. **Code Standards**:
   - Follow Google C++ Style Guide with 4-space indentation (enforced via `.clang-format`).
   - Ensure all hot-path functions remain `noexcept` with zero dynamic allocations (`malloc`/`new`).
   - Validate changes under AddressSanitizer (`-DENABLE_SANITIZERS=ON`).
