/*!
 * \file DLLHelper.hpp
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 * 
 * \brief 动态库辅助类,主要是把跨平台的差异封装起来,方便调用
 */
#pragma once
#include <string>

#include <dlfcn.h>
typedef void*		DllHandle;
typedef void*		ProcHandle;

class DLLHelper
{
public:
	static DllHandle load_library(const char *filename)
	{
		try
		{
		DllHandle ret = dlopen(filename, RTLD_NOW);
		if (ret == NULL)
			printf("%s\n", dlerror());
		return ret;
		}
		catch(...)
		{
			return NULL;
		}
	}

	static void free_library(DllHandle handle)
	{
		if (NULL == handle)
			return;

		dlclose(handle);
	}

	static ProcHandle get_symbol(DllHandle handle, const char* name)
	{
		if (NULL == handle)
			return NULL;

		return dlsym(handle, name);
	}

	static std::string wrap_module(const char* name, const char* unixPrefix = "lib")
	{
		std::size_t idx = 0;
		while (!isalpha(name[idx]))
			idx++;
		std::string ret(name, idx);
		ret.append(unixPrefix);
		ret.append(name + idx);
		ret += ".so";
		return std::move(ret);
	}
};