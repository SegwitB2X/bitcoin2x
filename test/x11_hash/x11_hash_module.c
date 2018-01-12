#include <Python.h>

#include "x11_hash.h"

static PyObject *x11_getpowhash(PyObject *self, PyObject *args)
{
    char *output;
    PyObject *value;
#if PY_MAJOR_VERSION >= 3
    PyBytesObject *input;
#else
    PyStringObject *input;
#endif
    if (!PyArg_ParseTuple(args, "S", &input))
        return NULL;
    Py_INCREF(input);
    output = PyMem_Malloc(32);

#if PY_MAJOR_VERSION >= 3
    x11_hash((char *)PyBytes_AsString((PyObject*) input), (int)PyBytes_Size((PyObject*) input), output);
#else
    x11_hash((char *)PyString_AsString((PyObject*) input), (int)PyString_Size((PyObject*) input), output);
#endif
    Py_DECREF(input);
#if PY_MAJOR_VERSION >= 3
    value = Py_BuildValue("y#", output, 32);
#else
    value = Py_BuildValue("s#", output, 32);
#endif
    PyMem_Free(output);
    return value;
}

static PyMethodDef X11Methods[] = {
    { "getPoWHash", x11_getpowhash, METH_VARARGS, "Returns the proof of work hash using x11 hash" },
    { NULL, NULL, 0, NULL }
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef X11Module = {
    PyModuleDef_HEAD_INIT,
    "x11_hash",
    "...",
    -1,
    X11Methods
};

PyMODINIT_FUNC PyInit_x11_hash(void) {
    return PyModule_Create(&X11Module);
}

#else

PyMODINIT_FUNC initx11_hash(void) {
    (void) Py_InitModule("x11_hash", X11Methods);
}
#endif
