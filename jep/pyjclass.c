/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 c-style: "K&R" -*- */
/* 
   jep - Java Embedded Python

   Copyright (C) 2004 Mike Johnson

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
*/ 	

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if HAVE_UNISTD_H
# include <sys/types.h>
# include <unistd.h>
#endif

// shut up the compiler
#ifdef _POSIX_C_SOURCE
#  undef _POSIX_C_SOURCE
#endif
#include <jni.h>

// shut up the compiler
#ifdef _POSIX_C_SOURCE
#  undef _POSIX_C_SOURCE
#endif
#include "Python.h"

#include "pyjfield.h"
#include "pyjclass.h"
#include "pyjobject.h"
#include "pyjmethod.h"
#include "util.h"

staticforward PyTypeObject PyJclass_Type;
staticforward PyMethodDef  pyjclass_methods[];

static void pyjclass_addmethod(PyJclass_Object*, PyObject*);
static void pyjclass_addfield(PyJclass_Object*, PyObject*);
static void pyjclass_dealloc(PyJclass_Object*);

static jmethodID classGetConstructors = 0;
static jmethodID classGetParmTypes    = 0;
static jmethodID classGetExceptions   = 0;


PyJclass_Object* pyjclass_new(JNIEnv *env, PyObject *pyjob) {
    PyJclass_Object  *pyc         = NULL;
    jobject           langClass   = NULL;
    jobjectArray      initArray   = NULL;
    int               i, len;
    PyJobject_Object *pyjobject  = NULL;

    pyc             = PyObject_NEW(PyJclass_Object, &PyJclass_Type);
    pyc->env        = env;
    pyc->initArray  = NULL;
    pyc->pyjobject  = pyjob;
    
    pyjobject = (PyJobject_Object *) pyjob;
    
    // ------------------------------ call Class.getConstructors()

    // well, first call getClass()
    if(classGetConstructors == 0) {
        jmethodID methodId;
        
        methodId = (*env)->GetMethodID(env,
                                       pyjobject->clazz,
                                       "getClass",
                                       "()Ljava/lang/Class;");
        if(process_java_exception(env) || !methodId)
            goto EXIT_ERROR;
        
        langClass = (*env)->CallObjectMethod(env, pyjobject->clazz, methodId);
        if(process_java_exception(env) || !langClass)
            goto EXIT_ERROR;
        
        // then, find getContructors()
        classGetConstructors =
            (*env)->GetMethodID(env,
                                langClass,
                                "getConstructors",
                                "()[Ljava/lang/reflect/Constructor;");
        if(process_java_exception(env) || !classGetConstructors)
            goto EXIT_ERROR;
    }
    
    // then, call method
    initArray = (jobjectArray) (*env)->CallObjectMethod(env,
                                                        pyjobject->clazz,
                                                        classGetConstructors);
    if(process_java_exception(env) || !initArray)
        goto EXIT_ERROR;

    pyc->initArray = (*env)->NewGlobalRef(env, initArray);
    pyc->initLen   = (*env)->GetArrayLength(env, pyc->initArray);
    
    if(langClass)
        (*env)->DeleteLocalRef(env, langClass);
    (*env)->DeleteLocalRef(env, initArray);
    return pyc;

EXIT_ERROR:
    if(langClass)
        (*env)->DeleteLocalRef(env, langClass);
    if(initArray)
        (*env)->DeleteLocalRef(env, initArray);
    if(pyc)
        pyjclass_dealloc(pyc);
    
    return NULL;
}


static void pyjclass_dealloc(PyJclass_Object *self) {
#if USE_DEALLOC
    JNIEnv *env = self->env;
    if(self->initArray)
        (*env)->DeleteGlobalRef(env, self->initArray);

    PyObject_Del(self);
#endif
}


// call constructor as a method and return pyjobject.
PyObject* pyjclass_call(PyJclass_Object *self,
                        PyObject *args,
                        PyObject *keywords) {
    int           initPos     = 0;
    int           parmPos     = 0;
    int           parmLen     = 0;
    jobjectArray  parmArray   = NULL;
    jobjectArray  exceptions  = NULL;
    JNIEnv       *env;
    jclass        initClass   = NULL;
    jobject       constructor = NULL;
    
    if(!PyTuple_Check(args)) {
        PyErr_Format(PyExc_RuntimeError, "args is not a valid tuple");
        return NULL;
    }
    
    if(keywords != NULL) {
        PyErr_Format(PyExc_RuntimeError, "Keywords are not supported.");
        return NULL;
    }

    env = self->env;
    
    // use a local frame so we don't have to worry too much about references.
    // make sure if this method errors out, that this is poped off again
    (*env)->PushLocalFrame(env, 20);
    if(process_java_exception(env))
        return NULL;

    
    for(initPos = 0; initPos < self->initLen; initPos++) {
        jmethodID     methodId;
        
        constructor = (*env)->GetObjectArrayElement(env,
                                                    self->initArray,
                                                    initPos);
        if(process_java_exception(env) || !constructor)
            goto EXIT_ERROR;
        
        
        // we need to get the parameters, first
        
        initClass = (*env)->GetObjectClass(env, constructor);
        if(process_java_exception(env) || !initClass)
            goto EXIT_ERROR;
        
        // next, get parameters for constructor
        if(classGetParmTypes == 0) {
            classGetParmTypes = (*env)->GetMethodID(env,
                                                    initClass,
                                                    "getParameterTypes",
                                                    "()[Ljava/lang/Class;");
            if(process_java_exception(env) || !classGetParmTypes)
                goto EXIT_ERROR;
        }
        
        parmArray = (jobjectArray) (*env)->CallObjectMethod(env,
                                                            constructor,
                                                            classGetParmTypes);
        if(process_java_exception(env) || !parmArray)
            goto EXIT_ERROR;

        
        // okay, we know how many parameters we need.
        // just discard the constructors with different counts.
        parmLen = (*env)->GetArrayLength(env, parmArray);
        if(PyTuple_Size(args) != parmLen) {
            (*env)->DeleteLocalRef(env, initClass);
            (*env)->DeleteLocalRef(env, parmArray);
            (*env)->DeleteLocalRef(env, constructor);
            continue;
        }

        
        // next, find matching constructor for args
        // the counts match but maybe not the args themselves.
        {
            jvalue jargs[parmLen];
            
            for(parmPos = 0; parmPos < parmLen; parmPos++) {
                PyObject *param       = PyTuple_GetItem(args, parmPos);
                int       paramTypeId = -1;
                jclass    pclazz;
                jobject   paramType   =
                    (jclass) (*env)->GetObjectArrayElement(env,
                                                           parmArray,
                                                           parmPos);
                
                if(process_java_exception(env) || !paramType) {
                    (*env)->DeleteLocalRef(env, paramType);
                    break;
                }
                
                pclazz = (*env)->GetObjectClass(env, paramType);
                if(process_java_exception(env) || !pclazz)
                    goto EXIT_ERROR;
                
                paramTypeId = get_jtype(env, paramType, pclazz);
                (*env)->DeleteLocalRef(env, pclazz);
                
                // if java and python agree, continue checking
                if(pyarg_matches_jtype(env, param, paramType, paramTypeId)) {
                    jargs[parmPos] = convert_pyarg_jvalue(env,
                                                          param,
                                                          paramType,
                                                          paramTypeId,
                                                          parmPos);
                    if(PyErr_Occurred() || process_java_exception(env))
                        goto EXIT_ERROR;
                    
                    (*env)->DeleteLocalRef(env, paramType);
                    continue;
                }
                
                (*env)->DeleteLocalRef(env, paramType);
                break;
                
            } // for each parameter type
            
            (*env)->DeleteLocalRef(env, parmArray);
            
            // did they match?
            if(parmPos == parmLen) {
                jobject   obj  = NULL;
                PyObject *pobj = NULL;
                
                if(PyErr_Occurred() || process_java_exception(env))
                    goto EXIT_ERROR;
                
                // get exceptions declared thrown
                
#if USE_MAPPED_EXCEPTIONS
                
                if(classGetExceptions == 0) {
                    classGetExceptions = (*env)->GetMethodID(
                        env,
                        initClass,
                        "getExceptionTypes",
                        "()[Ljava/lang/Class;");
                    if(process_java_exception(env) || !classGetExceptions)
                        goto EXIT_ERROR;
                }
                
                exceptions = (jobjectArray) (*env)->CallObjectMethod(
                    env,
                    constructor,
                    classGetExceptions);
                if(process_java_exception(env) || !exceptions)
                    goto EXIT_ERROR;

                if(!register_exceptions(env,
                                        initClass,
                                        constructor,
                                        exceptions))
                    goto EXIT_ERROR;

#endif
                
                // worked out, create new object
                
                methodId = (*env)->FromReflectedMethod(env,
                                                       constructor);
                if(process_java_exception(env))
                    goto EXIT_ERROR;
                (*env)->DeleteLocalRef(env, constructor);
                
                (*env)->PopLocalFrame(env, NULL);
                obj = (*env)->NewObjectA(env,
                                         ((PyJobject_Object* ) self->pyjobject)->clazz,
                                         methodId,
                                         jargs);
                if(process_java_exception(env) || !obj)
                    goto EXIT_ERROR;
                
                // finally, make pyjobject and return
                pobj = pyjobject_new(env, obj);
                
                // we already closed the local frame, so make
                // sure to delete this local ref.
                (*env)->DeleteLocalRef(env, obj);
                return pobj;
            }
            
        } // local scope
        
    } // for each constructor
    
    
    (*env)->PopLocalFrame(env, NULL);
    PyErr_Format(PyExc_RuntimeError, "Couldn't find matching constructor.");
    return NULL;
    
    
EXIT_ERROR:
    if(initClass)
        (*env)->DeleteLocalRef(env, initClass);
    if(parmArray)
        (*env)->DeleteLocalRef(env, parmArray);
    if(exceptions)
        (*env)->DeleteLocalRef(env, exceptions);
    
    (*env)->PopLocalFrame(env, NULL);
    
    return NULL;
}


static PyMethodDef pyjclass_methods[] = {
    {NULL, NULL, 0, NULL}
};


static PyTypeObject PyJclass_Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "PyJclass",
    sizeof(PyJclass_Object),
    0,
    (destructor) pyjclass_dealloc,            /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    0,                                        /* tp_hash  */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    0,                                        /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                       /* tp_flags */
    "jclass",                                 /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    pyjclass_methods,                         /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    0,                                        /* tp_init */
    0,                                        /* tp_alloc */
    NULL,                                     /* tp_new */
};