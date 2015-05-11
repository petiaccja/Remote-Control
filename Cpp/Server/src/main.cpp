#include <iostream>


#ifdef _MSC_VER
#include <conio.h>
#else 
char _getch() {
	return cin.get();
}
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////
// MAIN

int main() {

	cout << "Lekvaros kenyer juhuuuuuuuuuu" << endl;
	cout << "Press any key to exit...\n";
	_getch();
	return 0;
}
//
////////////////////////////////////////////////////////////////////////////////
