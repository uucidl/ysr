#include <stdio.h>

extern void printer(char const* value)
{
	printf ("Hello, World from library (%s)\n", value);
}
