// SPDX-License-Identifier: GPL-2.0
#include <Python.h>

int main(void)
{
	static struct PyModuleDef moduledef = {
		PyModuleDef_HEAD_INIT,
		.m_name = "test",
	};
	PyObject *module = PyModule_Create(&moduledef);

	return module ? 0 : -1;
}
