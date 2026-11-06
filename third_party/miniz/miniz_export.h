#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

/* 单文件内嵌用法：不需要 dllexport/dllimport，MINIZ_EXPORT 展开为空。
   （miniz 3.0.2 的发布包把这一句拆到了独立头里，这里补一个最小实现。） */
#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif

#endif /* MINIZ_EXPORT_H */
