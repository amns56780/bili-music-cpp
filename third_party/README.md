# third_party（源码内嵌的第三方依赖）

为了「克隆下来就能编译」，这些依赖直接内嵌在仓库里，不走 vcpkg / 包管理器。

| 目录 | 版本 | 许可 | 来源 |
| --- | --- | --- | --- |
| imgui/ | 1.93.0 | MIT | https://github.com/ocornut/imgui |
| glfw/ | 3.5.1 | zlib/libpng | https://github.com/glfw/glfw |
| nlohmann/json.hpp | 3.12.0 | MIT | https://github.com/nlohmann/json |

升级方式：替换对应目录/文件后，跑一遍 scripts/build.ps1 验证。
