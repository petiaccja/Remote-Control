
#ifdef _MSC_VER
#include <conio.h>
#else 
inline char _getch() {
	return std::cin.get();
}
#endif

int RcpTest();
int RcpBenchmark();
int RcsTest();