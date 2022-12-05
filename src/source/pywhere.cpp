#include "pywhere.hpp"

#include <Python.h>
#include <dlfcn.h>
#include <frameobject.h>

#include <mutex>
#include <vector>
#include <unordered_map>
#include <unistd.h>

class GIL {
 public:
  GIL() { _gstate = PyGILState_Ensure(); }
  ~GIL() { PyGILState_Release(_gstate); }

 private:
  PyGILState_STATE _gstate;
};

// NOTE: uncomment for debugging, but this causes issues
// for production builds on Alpine
//
// #include "printf.h"

int whereInPython(std::string& filename, int& lineno, int& bytei) {
  if (!Py_IsInitialized()) {  // No python, no python stack.
    return 0;
  }
  // This function walks the Python stack until it finds a frame
  // corresponding to a file we are actually profiling. On success,
  // it updates filename, lineno, and byte code index appropriately,
  // and returns 1.  If the stack walk encounters no such file, it
  // sets the filename to the pseudo-filename "<BOGUS>" for special
  // treatment within Scalene, and returns 0.
  filename = "<BOGUS>";
  lineno = 1;
  bytei = 0;
  GIL gil;
  // printf("ABCDE\n");

  PyThreadState* threadState = PyGILState_GetThisThreadState();
  auto frame = PyThreadState_GetFrame(threadState);
  Py_XDECREF(frame);
  
  // PyFrameObject* frame =
  //     threadState ? PyThreadState_GetFrame(threadState) : nullptr;
  // printf("%p\n", frame);
  // Py_XDECREF(frame);
return 0;
}

PyObject* placeholder(PyObject* self, PyObject* args) {
    auto p_where =
      (decltype(p_whereInPython)*)dlsym(RTLD_DEFAULT, "p_whereInPython");
  if (p_where == nullptr) {
    PyErr_SetString(PyExc_Exception, "Unable to find p_whereInPython");
    return NULL;
  }
  *p_where = whereInPython;


  Py_RETURN_NONE;
}

static PyMethodDef EmbMethods[] = {
    {"placeholder", placeholder, METH_NOARGS, ""},
    {NULL, NULL, 0, NULL}};

static PyModuleDef EmbedModule = {PyModuleDef_HEAD_INIT,
                                  "pywhere",
                                  NULL,
                                  -1,
                                  EmbMethods,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL};

PyMODINIT_FUNC PyInit_pywhere() { return PyModule_Create(&EmbedModule); }
