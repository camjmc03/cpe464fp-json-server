// The output of this program has been verified correct on vogon, falcon, and
// waldorf. You must use gcc when compiling on Solaris.  suncc will generate
// errors.

#include <stdio.h>

struct ex1
{
	char char1;
	short short1;
	int int1;
	char char2;
};

struct ex2
{
	char char1;
	char char2;
	short short1;
	int int1;
};

struct ex3
{
	char char1;
	short short1;
	int int1;
	char char2;
} __attribute__((packed));

void print_structs()
{
	printf(
"struct ex1\n"
"{\n"
"\tchar char1;\n"
"\tshort short1;\n"
"\tint int1;\n"
"\tchar char2;\n"
"};\n\n"

"struct ex2\n"
"{\n"
"\tchar char1;\n"
"\tchar char2;\n"
"\tshort short1;\n"
"\tint int1;\n"
"};\n\n"

"struct ex3\n"
"{\n"
"\tchar char1;\n"
"\tshort short1;\n"
"\tint int1;\n"
"\tchar char2;\n"
"} __attribute__((packed));\n\n\n");
}

int main()
{
	struct ex1 ex1 = { 1, 2, 3, 4};
	struct ex2 ex2 = { 1, 2, 3, 4};
	struct ex3 ex3 = { 1, 2, 3, 4};
	struct ex1 *pex1 = NULL;
	struct ex2 *pex2 = NULL;
	struct ex3 *pex3 = NULL;

	printf("sizeof(char)       == %d\n", sizeof(char));
	printf("sizeof(short)      == %d\n", sizeof(short));
	printf("sizeof(int)        == %d\n", sizeof(int));
	printf("sizeof(long)       == %d\n", sizeof(long));
	printf("sizeof(long long)  == %d\n\n", sizeof(long long));
	print_structs();

	printf("sizeof(struct ex1) == %d\n", sizeof(struct ex1));
		printf("\toffset of char1:  %d\n", (int)&pex1->char1);
		printf("\toffset of short1: %d\n", (int)&pex1->short1);
		printf("\toffset of int1:   %d\n", (int)&pex1->int1);
		printf("\toffset of char2: %d\n\n", (int)&pex1->char2);

	printf("sizeof(struct ex2) == %d\n", sizeof(struct ex2));
		printf("\toffset of char1:  %d\n", (int)&pex2->char1);
		printf("\toffset of char2: %d\n", (int)&pex2->char2);
		printf("\toffset of short1: %d\n", (int)&pex2->short1);
		printf("\toffset of int1:   %d\n\n", (int)&pex2->int1);

	printf("sizeof(struct ex3) == %d\n", sizeof(struct ex3));
		printf("\toffset of char1:  %d\n", (int)&pex3->char1);
		printf("\toffset of short1: %d\n", (int)&pex3->short1);
		printf("\toffset of int1:   %d\n", (int)&pex3->int1);
		printf("\toffset of char2: %d\n\n", (int)&pex3->char2);

	printf("directly accessing contents of ex1:\n");
		printf("\tchar1:  %d\n", ex1.char1);
		printf("\tshort1: %d\n", ex1.short1);
		printf("\tint1:   %d\n", ex1.int1);
		printf("\tchar2:  %d\n\n", ex1.char2);

	printf("directly accessing contents of ex2:\n");
		printf("\tchar1:  %d\n", ex2.char1);
		printf("\tchar2:  %d\n", ex2.char2);
		printf("\tshort1: %d\n", ex2.short1);
		printf("\tint1:   %d\n\n", ex2.int1);

	printf("directly accessing contents of ex3:\n");
		printf("\tchar1:  %d\n", ex3.char1);
		printf("\tshort1: %d\n", ex3.short1);
		printf("\tint1:   %d\n", ex3.int1);
		printf("\tchar2:  %d\n\n", ex3.char2);

	printf("indirectly accessing contents of ex1:\n");
		printf("\tchar1:  %d\n", *(&ex1.char1));
		printf("\tshort1: %d\n", *(&ex1.short1));
		printf("\tint1:   %d\n", *(&ex1.int1));
		printf("\tchar2:  %d\n\n", *(&ex1.char2));

	printf("indirectly accessing contents of ex2:\n");
		printf("\tchar1:  %d\n", *(&ex2.char1));
		printf("\tchar2:  %d\n", *(&ex2.char2));
		printf("\tshort1: %d\n", *(&ex2.short1));
		printf("\tint1:   %d\n\n", *(&ex2.int1));

	printf("indirectly accessing contents of ex3:\n");
		printf("\tchar1:  %d\n", *(&ex3.char1));
		printf("\tshort1: %d\n", *(&ex3.short1));
		printf("\tint1:   %d\n", *(&ex3.int1));
		printf("\tchar2:  %d\n\n", *(&ex3.char2));

	return 0;
}
