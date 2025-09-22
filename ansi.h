// Simple ANSI helpers
#define ANSI_WRAP(code, s) "\x1b[" code "m" s "\x1b[0m"

// Color/Bold wrappers for string *literals*
#define RED(s) ANSI_WRAP("31", s)
#define BLUE(s) ANSI_WRAP("34", s)
#define GREEN(s) ANSI_WRAP("32", s)
#define GRAY(s) ANSI_WRAP("90", s) // bright black = gray
#define ORANGE(s) ANSI_WRAP("33", s)
#define YELLOW(s) ANSI_WRAP("33", s)
#define PURPLE(s) ANSI_WRAP("35", s)
#define CYAN(s) ANSI_WRAP("36", s)
#define MAGENTA(s) ANSI_WRAP("35", s)
#define BOLD(s) ANSI_WRAP("1", s)

// Printf-style: use like printf(REDF("Error: %s\n"), msg);
#define REDF(fmt, ...) "\x1b[31m" fmt "\x1b[0m", ##__VA_ARGS__
#define BLUEF(fmt, ...) "\x1b[34m" fmt "\x1b[0m", ##__VA_ARGS__
#define GREENF(fmt, ...) "\x1b[32m" fmt "\x1b[0m", ##__VA_ARGS__
#define GRAYF(fmt, ...) "\x1b[90m" fmt "\x1b[0m", ##__VA_ARGS__
#define BOLDF(fmt, ...) "\x1b[1m" fmt "\x1b[0m", ##__VA_ARGS__
