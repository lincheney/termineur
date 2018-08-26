#ifndef UTILS_H
#define UTILS_H

#define STR_EQUAL(x, y) (strcmp((x), (y)) == 0)

// y must be static
#define STR_STARTSWITH(x, y) (strncmp((x), (y), sizeof(y)-1) == 0)

#define STR_STRIP_PREFIX(x, y) (STR_STARTSWITH((x), (y)) ? (x)+sizeof(y)-1 : NULL)

#endif
