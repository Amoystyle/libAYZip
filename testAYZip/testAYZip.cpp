// testAYZip.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "../libAYZip/libAYZip.h"
#ifndef NDEBUG
#pragma comment(lib, "../Debug/libAYZipd.lib")
#else
#pragma comment(lib, "../Release/libAYZip.lib")
#endif


int main()
{
    AYUnzipApp("C:\\Users\\Young\\Desktop\\Payload\\unc0ver-v5.0.1.ipa", "C:\\Users\\Young\\Desktop\\Payload\\Payload");

    //AYZipApp("C:\\Users\\Young\\Desktop\\Payload\\Payload\\unc0ver.app", "C:\\Users\\Young\\Desktop\\1.ipa");
    std::cout << "Hello World!\n";
}