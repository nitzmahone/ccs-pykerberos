/**
 * Copyright (c) 2006-2016 Apple Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#include <Python.h>

#include "kerberosbasic.h"
#include "kerberospw.h"
#include "kerberosgss.h"


/*
 * Support the Python 3 API while maintaining backward compatibility for the
 * Python 2 API.
 * Thanks to Lennart Regebro for http://python3porting.com/cextensions.html
 */
// Handle basic API changes
#if PY_MAJOR_VERSION >= 3
    // Basic renames (function parameters are the same)
    // No more int objects
    #define PyInt_FromLong PyLong_FromLong
    // CObjects to Capsules
    #define PyCObject_Check PyCapsule_CheckExact
    #define PyCObject_SetVoidPtr PyCapsule_SetPointer

    // More complex macros (function parameters are not the same)
    // Note for PyCObject_FromVoidPtr, destr is now the third parameter
    #define PyCObject_FromVoidPtr(cobj, destr) PyCapsule_New(cobj, NULL, destr)
    #define PyCObject_AsVoidPtr(pobj) PyCapsule_GetPointer(pobj, NULL)
#endif
// Handle differences in module definition syntax and interface
#if PY_MAJOR_VERSION >= 3
    #define MOD_ERROR_VAL NULL
    #define MOD_SUCCESS_VAL(val) val
    #define MOD_INIT(name) PyMODINIT_FUNC PyInit_##name(void)
    #define MOD_DEF(ob, name, doc, methods) \
          static struct PyModuleDef moduledef = { \
            PyModuleDef_HEAD_INIT, name, doc, -1, methods, }; \
          ob = PyModule_Create(&moduledef);
#else
    #define MOD_ERROR_VAL
    #define MOD_SUCCESS_VAL(val)
    #define MOD_INIT(name) void init##name(void)
    #define MOD_DEF(ob, name, doc, methods) \
          ob = Py_InitModule3(name, methods, doc);
#endif

static char krb5_mech_oid_bytes [] = "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02";
gss_OID_desc krb5_mech_oid = { 9, &krb5_mech_oid_bytes };

static char spnego_mech_oid_bytes[] = "\x2b\x06\x01\x05\x05\x02";
gss_OID_desc spnego_mech_oid = { 6, &spnego_mech_oid_bytes };

char STATE_NULL_C = 'C';
void* STATE_NULL = &STATE_NULL_C;

PyObject *KrbException_class;
PyObject *BasicAuthException_class;
PyObject *PwdChangeException_class;
PyObject *GssException_class;

static PyObject *checkPassword(PyObject *self, PyObject *args)
{
    const char *user = NULL;
    const char *pswd = NULL;
    const char *service = NULL;
    const char *default_realm = NULL;
    int result = 0;

    if (! PyArg_ParseTuple(args, "ssss", &user, &pswd, &service, &default_realm)) {
        return NULL;
    }

    result = authenticate_user_krb5pwd(user, pswd, service, default_realm);

    if (result) {
        return Py_INCREF(Py_True), Py_True;
    } else {
        return NULL;
    }
}

static PyObject *changePassword(PyObject *self, PyObject *args)
{
    const char *newpswd = NULL;
    const char *oldpswd = NULL;
    const char *user = NULL;
    int result = 0;

    if (! PyArg_ParseTuple(args, "sss", &user, &oldpswd, &newpswd)) {
        return NULL;
    }

    result = change_user_krb5pwd(user, oldpswd, newpswd);

    if (result) {
        return Py_INCREF(Py_True), Py_True;
    } else {
        return NULL;
    }
}

static PyObject *getServerPrincipalDetails(PyObject *self, PyObject *args)
{
    const char *service = NULL;
    const char *hostname = NULL;
    char* result = NULL;

    if (! PyArg_ParseTuple(args, "ss", &service, &hostname)) {
        return NULL;
    }

    result = server_principal_details(service, hostname);

    if (result != NULL) {
        PyObject* pyresult = Py_BuildValue("s", result);
        free(result);
        return pyresult;
    } else {
        return NULL;
    }
}

static PyObject* authGSSClientInit(PyObject* self, PyObject* args, PyObject* keywds)
{
    const char *service = NULL;
    const char *principal = NULL;
    gss_client_state *state = NULL;
    PyObject *pystate = NULL;
    gss_server_state *delegatestate = NULL;
    PyObject *pydelegatestate = NULL;
    gss_OID mech_oid = GSS_C_NO_OID;
    PyObject *pymech_oid = NULL;
    static char *kwlist[] = {
        "service", "principal", "gssflags", "delegated", "mech_oid", NULL
    };
    long int gss_flags = GSS_C_MUTUAL_FLAG | GSS_C_SEQUENCE_FLAG;
    int result = 0;

    if (! PyArg_ParseTupleAndKeywords(
        args, keywds, "s|zlOO", kwlist,
        &service, &principal, &gss_flags, &pydelegatestate, &pymech_oid
    )) {
        return NULL;
    }

    state = (gss_client_state *) malloc(sizeof(gss_client_state));
    if (state == NULL)
    {
        PyErr_NoMemory();
        return NULL;
    }
    pystate = PyCObject_FromVoidPtr(state, NULL);

    if (pydelegatestate != NULL && PyCObject_Check(pydelegatestate)) {
        delegatestate = PyCObject_AsVoidPtr(pydelegatestate);
    }

    if (pymech_oid != NULL && PyCapsule_CheckExact(pymech_oid)) {
        const char * mech_oid_name = PyCapsule_GetName(pymech_oid);
        mech_oid = PyCapsule_GetPointer(pymech_oid, mech_oid_name);
    }

    result = authenticate_gss_client_init(
        service, principal, gss_flags, delegatestate, mech_oid, state
    );

    if (result == AUTH_GSS_ERROR) {
        return NULL;
    }

    return Py_BuildValue("(iO)", result, pystate);
}

static PyObject *authGSSClientClean(PyObject *self, PyObject *args)
{
    gss_client_state *state = NULL;
    PyObject *pystate = NULL;
    int result = 0;

    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }

    if (!PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_client_state *)PyCObject_AsVoidPtr(pystate);

    if (state != STATE_NULL) {
        result = authenticate_gss_client_clean(state);

        free(state);
        PyCObject_SetVoidPtr(pystate, STATE_NULL);
    }

    return Py_BuildValue("i", result);
}

static PyObject *authGSSClientStep(PyObject *self, PyObject *args)
{
    gss_client_state *state = NULL;
    PyObject *pystate = NULL;
    char *challenge = NULL;
    int result = 0;

    if (! PyArg_ParseTuple(args, "Os", &pystate, &challenge)) {
        return NULL;
    }

    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_client_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }

    result = authenticate_gss_client_step(state, challenge);

    if (result == AUTH_GSS_ERROR) {
        return NULL;
    }

    return Py_BuildValue("i", result);
}

static PyObject *authGSSClientResponseConf(PyObject *self, PyObject *args)
{
    gss_client_state *state = NULL;
    PyObject *pystate = NULL;

    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }

    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_client_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }

    return Py_BuildValue("i", state->responseConf);
}

static PyObject *authGSSServerHasDelegated(PyObject *self, PyObject *args)
{
    gss_server_state *state = NULL;
    PyObject *pystate = NULL;

    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }

    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_server_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }

    return PyBool_FromLong(authenticate_gss_server_has_delegated(state));
}

static PyObject *authGSSClientResponse(PyObject *self, PyObject *args)
{
    gss_client_state *state = NULL;
    PyObject *pystate = NULL;

    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }

    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_client_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }

    return Py_BuildValue("s", state->response);
}

static PyObject *authGSSClientUserName(PyObject *self, PyObject *args)
{
    gss_client_state *state = NULL;
    PyObject *pystate = NULL;

    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }

    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_client_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }

    return Py_BuildValue("s", state->username);
}

static PyObject *authGSSClientUnwrap(PyObject *self, PyObject *args)
{
	gss_client_state *state = NULL;
	PyObject *pystate = NULL;
	char *challenge = NULL;
	int result = 0;

	if (! PyArg_ParseTuple(args, "Os", &pystate, &challenge)) {
		return NULL;
    }

	if (! PyCObject_Check(pystate)) {
		PyErr_SetString(PyExc_TypeError, "Expected a context object");
		return NULL;
	}

	state = (gss_client_state *)PyCObject_AsVoidPtr(pystate);

	if (state == STATE_NULL) {
		return NULL;
    }

	result = authenticate_gss_client_unwrap(state, challenge);

	if (result == AUTH_GSS_ERROR) {
		return NULL;
    }

	return Py_BuildValue("i", result);
}

static PyObject *authGSSClientWrap(PyObject *self, PyObject *args)
{
	gss_client_state *state = NULL;
	PyObject *pystate = NULL;
	char *challenge = NULL;
	char *user = NULL;
	int protect = 0;
	int result = 0;

	if (! PyArg_ParseTuple(
        args, "Os|zi", &pystate, &challenge, &user, &protect
    )) {
		return NULL;
    }

	if (! PyCObject_Check(pystate)) {
		PyErr_SetString(PyExc_TypeError, "Expected a context object");
		return NULL;
	}

	state = (gss_client_state *)PyCObject_AsVoidPtr(pystate);

	if (state == STATE_NULL) {
		return NULL;
    }

	result = authenticate_gss_client_wrap(state, challenge, user, protect);

	if (result == AUTH_GSS_ERROR) {
		return NULL;
    }

	return Py_BuildValue("i", result);
}

static PyObject* authGSSEncryptMessage(PyObject* self, PyObject* args)
{
    char *input = NULL;
    char *header = NULL;
    int header_len = 0;
    char *enc_output = NULL;
    int enc_output_len = 0;
    PyObject *pystate = NULL;
    gss_client_state *state = NULL;
    int result = 0;
    PyObject *pyresult = NULL;

    // NB: use et so we get a copy of the string (since gss_wrap_iov mutates it), and so we're certain it's always
    // a UTF8 byte string
    if (! PyArg_ParseTuple(args, "Oet", &pystate, "UTF-8", &input)) {
        pyresult = NULL;
        goto end;
    }

    if (!PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        pyresult = NULL;
        goto end;
    }

    state = (gss_client_state *)PyCObject_AsVoidPtr(pystate);
    if (state == STATE_NULL) {
        pyresult = NULL;
        goto end;
    }

    result = encrypt_message(state, input, &header, &header_len, &enc_output, &enc_output_len);

    if (result == AUTH_GSS_ERROR) {
        pyresult = NULL;
        goto end;
    }

    pyresult = Py_BuildValue("s# s#", enc_output, enc_output_len, header, header_len);
end:
    if (input) {
        PyMem_Free(input);
    }
    if (header) {
        free(header);
    }
    if (enc_output) {
        free(enc_output);
    }

    return pyresult;
}

static PyObject* authGSSDecryptMessage(PyObject* self, PyObject* args)
{
    char *header = NULL;
    int header_len = 0;
    char *enc_data = NULL;
    int enc_data_len = 0;
    PyObject *pystate = NULL;
    PyObject *pyheader = NULL;
    PyObject *pyenc_data = NULL;
    gss_client_state *state = NULL;
    char *dec_output = NULL;
    int dec_output_len = 0;
    int result = 0;
    PyObject *pyresult = 0;

    // NB: since the sig/data strings are arbitrary binary and don't conform to
    // a valid encoding, none of the normal string marshaling types will work. We'll
    // have to extract the data later.
    if (! PyArg_ParseTuple(args, "OOO", &pystate, &pyenc_data, &pyheader)) {
        pyresult = NULL;
        goto end;
    }

    if (!PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        pyresult = NULL;
        goto end;
    }

    state = (gss_client_state *)PyCObject_AsVoidPtr(pystate);
    if (state == STATE_NULL) {
        pyresult = NULL;
        goto end;
    }

    // request the length and copy the header and encrypted input data from the Python strings
    header_len = (int) PyString_Size(pyheader);
    header = malloc(header_len);
    memcpy(header, PyString_AS_STRING(pyheader), header_len);

    enc_data_len = (int) PyString_Size(pyenc_data);
    enc_data = malloc(enc_data_len);
    memcpy(enc_data, PyString_AS_STRING(pyenc_data), enc_data_len);

    result = decrypt_message(state, header, header_len, enc_data, enc_data_len, &dec_output, &dec_output_len);

    if (result == AUTH_GSS_ERROR) {
        pyresult = NULL;
        goto end;
    }

    pyresult = Py_BuildValue("s#", dec_output, dec_output_len);
end:
    if (header) {
        free(header);
    }
    if (enc_data) {
        free(enc_data);
    }
    if (dec_output) {
        free(dec_output);
    }

    return pyresult;
}


static PyObject *authGSSClientInquireCred(PyObject *self, PyObject *args)
{
    gss_client_state *state = NULL;
    PyObject *pystate = NULL;
    int result = 0;
    if (!PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }

    if (!PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_client_state *)PyCObject_AsVoidPtr(pystate);
    if (state == STATE_NULL) {
        return NULL;
    }

    result = authenticate_gss_client_inquire_cred(state);
    if (result == AUTH_GSS_ERROR) {
        return NULL;
    }

    return Py_BuildValue("i", result);
}

static PyObject *authGSSServerInit(PyObject *self, PyObject *args)
{
    const char *service = NULL;
    gss_server_state *state = NULL;
    PyObject *pystate = NULL;
    int result = 0;

    if (! PyArg_ParseTuple(args, "s", &service)) {
        return NULL;
    }

    state = (gss_server_state *) malloc(sizeof(gss_server_state));
    if (state == NULL)
    {
        PyErr_NoMemory();
        return NULL;
    }
    pystate = PyCObject_FromVoidPtr(state, NULL);

    result = authenticate_gss_server_init(service, state);

    if (result == AUTH_GSS_ERROR) {
        return NULL;
    }

    return Py_BuildValue("(iO)", result, pystate);
}

static PyObject *authGSSServerClean(PyObject *self, PyObject *args)
{
    gss_server_state *state = NULL;
    PyObject *pystate = NULL;
    int result = 0;

    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }

    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_server_state *)PyCObject_AsVoidPtr(pystate);

    if (state != STATE_NULL) {
        result = authenticate_gss_server_clean(state);

        free(state);
        PyCObject_SetVoidPtr(pystate, STATE_NULL);
    }

    return Py_BuildValue("i", result);
}

static PyObject *authGSSServerStep(PyObject *self, PyObject *args)
{
    gss_server_state *state = NULL;
    PyObject *pystate = NULL;
    char *challenge = NULL;
    int result = 0;

    if (! PyArg_ParseTuple(args, "Os", &pystate, &challenge)) {
        return NULL;
    }

    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_server_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }

    result = authenticate_gss_server_step(state, challenge);

    if (result == AUTH_GSS_ERROR) {
        return NULL;
    }

    return Py_BuildValue("i", result);
}

static PyObject *authGSSServerStoreDelegate(PyObject *self, PyObject *args)
{
    gss_server_state *state = NULL;
    PyObject *pystate = NULL;
    int result = 0;

    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }

    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_server_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }

    result = authenticate_gss_server_store_delegate(state);

    if (result == AUTH_GSS_ERROR) {
        return NULL;
    }
    
    return Py_BuildValue("i", result);
}

static PyObject *authGSSServerResponse(PyObject *self, PyObject *args)
{
    gss_server_state *state = NULL;
    PyObject *pystate = NULL;

    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }

    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }

    state = (gss_server_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }

    return Py_BuildValue("s", state->response);
}

static PyObject *authGSSServerUserName(PyObject *self, PyObject *args)
{
    gss_server_state *state = NULL;
    PyObject *pystate = NULL;
    
    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }
    
    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }
    
    state = (gss_server_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }
    
    return Py_BuildValue("s", state->username);
}

static PyObject *authGSSServerCacheName(PyObject *self, PyObject *args)
{
    gss_server_state *state = NULL;
    PyObject *pystate = NULL;
    
    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }
    
    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }
    
    state = (gss_server_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }

    return Py_BuildValue("s", state->ccname);
}

static PyObject *authGSSServerTargetName(PyObject *self, PyObject *args)
{
    gss_server_state *state = NULL;
    PyObject *pystate = NULL;
    
    if (! PyArg_ParseTuple(args, "O", &pystate)) {
        return NULL;
    }
    
    if (! PyCObject_Check(pystate)) {
        PyErr_SetString(PyExc_TypeError, "Expected a context object");
        return NULL;
    }
    
    state = (gss_server_state *)PyCObject_AsVoidPtr(pystate);

    if (state == STATE_NULL) {
        return NULL;
    }
    
    return Py_BuildValue("s", state->targetname);
}

static PyMethodDef KerberosMethods[] = {
    {
        "checkPassword",
        checkPassword, METH_VARARGS,
        "Check the supplied user/password against Kerberos KDC."
    },
    {
        "changePassword",
        changePassword, METH_VARARGS,
        "Change the user password."
    },
    {
        "getServerPrincipalDetails",
        getServerPrincipalDetails, METH_VARARGS,
        "Return the service principal for a given service and hostname."
    },
    {
        "authGSSEncryptMessage",
        authGSSEncryptMessage, METH_VARARGS,
        "Encrypt a message"
    },

    {
        "authGSSDecryptMessage",
        authGSSDecryptMessage, METH_VARARGS,
        "Decrypt a message"
    },
    {
        "authGSSClientInit",
        (PyCFunction)authGSSClientInit, METH_VARARGS | METH_KEYWORDS,
        "Initialize client-side GSSAPI operations."
    },
    {
        "authGSSClientClean",
        authGSSClientClean, METH_VARARGS,
        "Terminate client-side GSSAPI operations."
    },
    {
        "authGSSClientStep",
        authGSSClientStep, METH_VARARGS,
        "Do a client-side GSSAPI step."
    },
    {
        "authGSSClientResponse",
        authGSSClientResponse, METH_VARARGS,
        "Get the response from the last client-side GSSAPI step."
    },
    {
        "authGSSClientInquireCred",  authGSSClientInquireCred, METH_VARARGS,
        "Get the current user name, if any, without a client-side GSSAPI step"
    },
    {
        "authGSSClientResponseConf",
        authGSSClientResponseConf, METH_VARARGS,
        "return 1 if confidentiality was set in the last unwrapped buffer, 0 otherwise."
    },
    {
        "authGSSClientUserName",
        authGSSClientUserName, METH_VARARGS,
        "Get the user name from the last client-side GSSAPI step."
    },
    {
        "authGSSServerInit",
        authGSSServerInit, METH_VARARGS,
        "Initialize server-side GSSAPI operations."
    },
    {
        "authGSSClientWrap",
        authGSSClientWrap, METH_VARARGS,
        "Do a GSSAPI wrap."
    },
    {
        "authGSSClientUnwrap",
        authGSSClientUnwrap, METH_VARARGS,
        "Do a GSSAPI unwrap."
    },
    {
        "authGSSClientInquireCred", authGSSClientInquireCred, METH_VARARGS,
        "Get the current user name, if any."
    },
    {
        "authGSSServerClean",
        authGSSServerClean, METH_VARARGS,
        "Terminate server-side GSSAPI operations."
    },
    {
        "authGSSServerStep",
        authGSSServerStep, METH_VARARGS,
        "Do a server-side GSSAPI step."
    },
    {
        "authGSSServerHasDelegated",
        authGSSServerHasDelegated, METH_VARARGS,
        "Check whether the client delegated credentials to us."
    },
    {
        "authGSSServerStoreDelegate",
        authGSSServerStoreDelegate, METH_VARARGS,
        "Store the delegated Credentials."
    },
    {
        "authGSSServerResponse",
        authGSSServerResponse, METH_VARARGS,
        "Get the response from the last server-side GSSAPI step."
    },
    {
        "authGSSServerUserName",
        authGSSServerUserName, METH_VARARGS,
        "Get the user name from the last server-side GSSAPI step."
    },
    {
        "authGSSServerCacheName",
        authGSSServerCacheName, METH_VARARGS,
        "Get the location of the cache where delegated credentials are stored."
    },
    {
        "authGSSServerTargetName",
        authGSSServerTargetName, METH_VARARGS,
        "Get the target name from the last server-side GSSAPI step."
    },
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

MOD_INIT(kerberos)
{
    PyObject *m,*d;

    MOD_DEF(m, "kerberos", NULL, KerberosMethods);

    if (m == NULL) {
        return MOD_ERROR_VAL;
    }

    d = PyModule_GetDict(m);

    /* create the base exception class */
    if (! (KrbException_class = PyErr_NewException(
        "kerberos.KrbError", NULL, NULL
    ))) {
        goto error;
    }

    PyDict_SetItemString(d, "KrbError", KrbException_class);
    Py_INCREF(KrbException_class);

    /* ...and the derived exceptions */
    if (! (BasicAuthException_class = PyErr_NewException(
        "kerberos.BasicAuthError", KrbException_class, NULL
    ))) {
        goto error;
    }

    Py_INCREF(BasicAuthException_class);
    PyDict_SetItemString(d, "BasicAuthError", BasicAuthException_class);

    if (! (PwdChangeException_class = PyErr_NewException(
        "kerberos.PwdChangeError", KrbException_class, NULL
    ))) {
        goto error;
    }

    Py_INCREF(PwdChangeException_class);
    PyDict_SetItemString(d, "PwdChangeError", PwdChangeException_class);

    if (! (GssException_class = PyErr_NewException(
        "kerberos.GSSError", KrbException_class, NULL
    ))) {
        goto error;
    }

    Py_INCREF(GssException_class);
    PyDict_SetItemString(
        d, "GSSError", GssException_class
    );

    PyDict_SetItemString(
        d, "AUTH_GSS_COMPLETE", PyInt_FromLong(AUTH_GSS_COMPLETE)
    );
    PyDict_SetItemString(
        d, "AUTH_GSS_CONTINUE", PyInt_FromLong(AUTH_GSS_CONTINUE)
    );

    PyDict_SetItemString(
        d, "GSS_C_DELEG_FLAG", PyInt_FromLong(GSS_C_DELEG_FLAG)
    );
    PyDict_SetItemString(
        d, "GSS_C_MUTUAL_FLAG", PyInt_FromLong(GSS_C_MUTUAL_FLAG)
    );
    PyDict_SetItemString(
        d, "GSS_C_REPLAY_FLAG", PyInt_FromLong(GSS_C_REPLAY_FLAG)
    );
    PyDict_SetItemString(
        d, "GSS_C_SEQUENCE_FLAG", PyInt_FromLong(GSS_C_SEQUENCE_FLAG)
    );
    PyDict_SetItemString(
        d, "GSS_C_CONF_FLAG", PyInt_FromLong(GSS_C_CONF_FLAG)
    );
    PyDict_SetItemString(
        d, "GSS_C_INTEG_FLAG", PyInt_FromLong(GSS_C_INTEG_FLAG)
    );
    PyDict_SetItemString(
        d, "GSS_C_ANON_FLAG", PyInt_FromLong(GSS_C_ANON_FLAG)
    );
    PyDict_SetItemString(
        d, "GSS_C_PROT_READY_FLAG", PyInt_FromLong(GSS_C_PROT_READY_FLAG)
    );
    PyDict_SetItemString(
        d, "GSS_C_TRANS_FLAG", PyInt_FromLong(GSS_C_TRANS_FLAG)
    );
    PyDict_SetItemString(
        d, "GSS_MECH_OID_KRB5", PyCapsule_New(&krb5_mech_oid, "kerberos.GSS_MECH_OID_KRB5", NULL)
    );
    PyDict_SetItemString(
        d, "GSS_MECH_OID_SPNEGO", PyCapsule_New(&spnego_mech_oid, "kerberos.GSS_MECH_OID_SPNEGO", NULL)
    );

error:
    if (PyErr_Occurred()) {
         PyErr_SetString(PyExc_ImportError, "kerberos: init failed");
        return MOD_ERROR_VAL;
    }

    return MOD_SUCCESS_VAL(m);
}
