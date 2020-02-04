#include <iostream>

enum type { unknown, type_a = 10, type_b, type_c = 5, type_d };

int main()
{
	printf("unknown = %d\n", unknown);
	printf("type_a = %d\n", type_a);
	printf("type_b = %d\n", type_b);
	printf("type_c = %d\n", type_c);
	printf("type_d = %d\n", type_d);
	return 0;
}
