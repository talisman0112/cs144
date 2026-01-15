设置构建系统：
cmake -S . -B build
编译项目：
cmake --build build
运行测试：
cmake --build build --target check[checkpoint_num]   # 只运行某个 checkpoint 的测试（可选）
或者运行所有测试：
cmake --build build --target test
运行性能基准测试：
cmake --build build --target speed
运行 clang-tidy（静态分析工具，会给出代码改进建议）：
cmake --build build --target tidy
格式化代码（使用 clang-format 自动美化代码）：
cmake --build build --target format
额外教学小贴士（帮你更快上手）

第一次使用时，先运行设置构建系统的命令：
cmake -S . -B build
这会在项目根目录下创建一个 build 文件夹，里面放所有编译中间文件（保持源码目录干净）。
日常开发流程（推荐）：
cmake -S . -B build          # 只在第一次或改了 CMakeLists.txt 时运行
cmake --build build          # 编译
cmake --build build --target test    # 运行所有测试，看哪些没过
改代码 → 重新编译 → 再跑测试，循环往复。
只测当前 Lab 的某个 checkpoint（非常实用）：
比如 Lab 0 有 checkpoint 1，就运行：
cmake --build build --target check1
这样可以快速验证当前进度，不用等所有测试跑完。
format 和 tidy 是你的好朋友：
format：一键让代码风格统一（CS144 对代码格式要求严格，交之前一定要跑）。
tidy：会指出潜在 bug、现代 C++ 改进建议，多跑几次能大幅提升代码质量。