#include "pch.h"
int main()
{
	HANDLE hFile = CreateFile(TEXT("d:/hello.txt"), GENERIC_READ_, OPEN_EXISTING_, 0);
	if (NULL == hFile)
		return -1;
	char szBuffer[10000 + 1];
	DWORD dwReadSize = 0;
	ReadFile(hFile, szBuffer, 10000, &dwReadSize);
	szBuffer[dwReadSize] = 0;
	printf("%s\n", szBuffer);
	CloseFile(hFile);
	return 0;
}

// 이중 백슬래시 or 슬래시 사용
//#pragma comment(lib, "d:/GIT/cppcore/Build/x64ReleaseMT/cppcore.lib")
//
//int main() {
//
//	return 0;
//}