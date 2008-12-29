/*
 * Some additional deconvolution functions for AIPY, written in C++.  These are
 * mostly for speed-critical applications. 
 *
 * Author: Aaron Parsons
 */

#include <Python.h>
#include "numpy/arrayobject.h"

#define QUOTE(s) # s

#define PNT1(a,i) (a->data + i*a->strides[0])
#define PNT2(a,i,j) (a->data+i*a->strides[0]+j*a->strides[1])
#define IND1(a,i,type) *((type *)PNT1(a,i))
#define IND2(a,i,j,type) *((type *)PNT2(a,i,j))
#define CIND1R(a,i,type) *((type *)PNT1(a,i))
#define CIND1I(a,i,type) *((type *)(PNT1(a,i)+sizeof(type)))
#define CIND2R(a,i,j,type) *((type *)PNT2(a,i,j))
#define CIND2I(a,i,j,type) *((type *)(PNT2(a,i,j)+sizeof(type)))

#define TYPE(a) a->descr->type_num
#define CHK_ARRAY_TYPE(a,type) \
    if (TYPE(a) != type) { \
        PyErr_Format(PyExc_ValueError, "type(%s) != %s", \
        QUOTE(a), QUOTE(type)); \
        return NULL; }

#define DIM(a,i) a->dimensions[i]
#define CHK_ARRAY_DIM(a,i,d) \
    if (DIM(a,i) != d) { \
        PyErr_Format(PyExc_ValueError, "dim(%s) != %s", \
        QUOTE(a), QUOTE(d)); \
        return NULL; }

#define RANK(a) a->nd
#define CHK_ARRAY_RANK(a,r) \
    if (RANK(a) != r) { \
        PyErr_Format(PyExc_ValueError, "rank(%s) != %s", \
        QUOTE(a), QUOTE(r)); \
        return NULL; }


// A template for implementing addition loops for different data types
template<typename T> struct Clean {

    //   ____ _                  ____     _      
    //  / ___| | ___  __ _ _ __ |___ \ __| |_ __ 
    // | |   | |/ _ \/ _` | '_ \  __) / _` | '__|
    // | |___| |  __/ (_| | | | |/ __/ (_| | |   
    //  \____|_|\___|\__,_|_| |_|_____\__,_|_|   
    // Does a 2d real-valued clean
    static int clean_2d_r(PyArrayObject *res, PyArrayObject *ker,
            PyArrayObject *mdl, double gain, int maxiter, 
            double tol, int verb) {
        T score=-1, nscore, max=0, mmax, val, mval, step, q=0, mq=0;
        T firstscore=-1;
        int argmax1=0, argmax2=0, nargmax1=0, nargmax2=0;
        int dim1=DIM(res,0), dim2=DIM(res,1), wrap_n1, wrap_n2;
        // Compute gain/phase of kernel
        for (int n1=0; n1 < dim1; n1++) {
            for (int n2=0; n2 < dim2; n2++) {
                val = IND2(ker,n1,n2,T);
                mval = val * val;
                if (mval > mq) {
                    mq = mval;
                    q = val;
                }
            }
        }
        q = 1/q;
        // The clean loop
        for (int i=0; i < maxiter; i++) {
            nscore = 0;
            mmax = -1;
            step = (T) gain * max * q;
            IND2(mdl,argmax1,argmax2,T) += step;
            // Take next step and compute score
            for (int n1=0; n1 < dim1; n1++) {
                wrap_n1 = (n1 + argmax1) % dim1;
                for (int n2=0; n2 < dim2; n2++) {
                    wrap_n2 = (n2 + argmax2) % dim2;
                    IND2(res,wrap_n1,wrap_n2,T) -= IND2(ker,n1,n2,T) * step;
                    val = IND2(res,wrap_n1,wrap_n2,T);
                    mval = val * val;
                    nscore += mval;
                    if (mval > mmax) {
                        nargmax1 = wrap_n1; nargmax2 = wrap_n2;
                        max = val;
                        mmax = mval;
                    }
                }
            }
            nscore = sqrt(nscore / (dim1 * dim2));
            if (firstscore < 0) firstscore = nscore;
            if (verb != 0)
                printf("Iter %d: Max=(%d,%d), Score = %f, Prev = %f\n", \
                    i, nargmax1, nargmax2, (double) (nscore/firstscore), \
                    (double) (score/firstscore));
            if (score > 0 && nscore > score) {
                // We've diverged: undo last step and give up
                IND2(mdl,argmax1,argmax2,T) -= step;
                for (int n1=0; n1 < dim1; n1++) {
                    wrap_n1 = (n1 + argmax1) % dim1;
                    for (int n2=0; n2 < dim2; n2++) {
                        wrap_n2 = (n2 + argmax2) % dim2;
                        IND2(res,wrap_n1,wrap_n2,T) += IND2(ker,n1,n2,T) * step;
                    }
                }
                return -i;
            } else if (score > 0 && (score - nscore) / firstscore < tol) {
                // We're done
                return i;
            }
            score = nscore;
            argmax1 = nargmax1; argmax2 = nargmax2;
        }
        return maxiter;
    }
    //   ____ _                  _     _      
    //  / ___| | ___  __ _ _ __ / | __| |_ __ 
    // | |   | |/ _ \/ _` | '_ \| |/ _` | '__|
    // | |___| |  __/ (_| | | | | | (_| | |   
    //  \____|_|\___|\__,_|_| |_|_|\__,_|_|   
    // Does a 1d real-valued clean
    static int clean_1d_r(PyArrayObject *res, PyArrayObject *ker, 
            PyArrayObject *mdl, double gain, int maxiter, double tol,
            int verb) {
        T score=-1, nscore, max=0, mmax, val, mval, step, q=0, mq=0;
        T firstscore=-1;
        int argmax=0, nargmax=0, dim=DIM(res,0), wrap_n;
        // Compute gain/phase of kernel
        for (int n=0; n < dim; n++) {
            val = IND1(ker,n,T);
            mval = val * val;
            if (mval > mq) {
                mq = mval;
                q = val;
            }
        }
        q = 1/q;
        // The clean loop
        for (int i=0; i < maxiter; i++) {
            nscore = 0;
            mmax = -1;
            step = (T) gain * max * q;
            IND1(mdl,argmax,T) += step;
            // Take next step and compute score
            for (int n=0; n < dim; n++) {
                wrap_n = (n + argmax) % dim;
                IND1(res,wrap_n,T) -= IND1(ker,n,T) * step;
                val = IND1(res,wrap_n,T);
                mval = val * val;
                nscore += mval;
                if (mval > mmax) {
                    nargmax = wrap_n;
                    max = val;
                    mmax = mval;
                }
            }
            nscore = sqrt(nscore / dim);
            if (firstscore < 0) firstscore = nscore;
            if (verb != 0)
                printf("Iter %d: Max=(%d), Score = %f, Prev = %f\n", \
                    i, nargmax, (double) (nscore/firstscore), \
                    (double) (score/firstscore));
            if (score > 0 && nscore > score) {
                // We've diverged: undo last step and give up
                IND1(mdl,argmax,T) -= step;
                for (int n=0; n < dim; n++) {
                    wrap_n = (n + argmax) % dim;
                    IND1(res,wrap_n,T) += IND1(ker,n,T) * step;
                }
                return -i;
            } else if (score > 0 && (score - nscore) / firstscore < tol) {
                // We're done
                return i;
            }
            score = nscore;
            argmax = nargmax;
        }
        return maxiter;
    }
    //   ____ _                  ____     _      
    //  / ___| | ___  __ _ _ __ |___ \ __| | ___ 
    // | |   | |/ _ \/ _` | '_ \  __) / _` |/ __|
    // | |___| |  __/ (_| | | | |/ __/ (_| | (__ 
    //  \____|_|\___|\__,_|_| |_|_____\__,_|\___|
    // Does a 2d complex-valued clean
    static int clean_2d_c(PyArrayObject *res, PyArrayObject *ker,
            PyArrayObject *mdl, double gain, int maxiter, double tol,
            int verb) {
        T maxr=0, maxi=0, valr, vali, stepr, stepi, qr=0, qi=0;
        T score=-1, nscore, mmax, mval, mq=0;
        T firstscore=-1;
        int argmax1=0, argmax2=0, nargmax1=0, nargmax2=0;
        int dim1=DIM(res,0), dim2=DIM(res,1), wrap_n1, wrap_n2;
        // Compute gain/phase of kernel
        for (int n1=0; n1 < dim1; n1++) {
            for (int n2=0; n2 < dim2; n2++) {
                valr = CIND2R(ker,n1,n2,T);
                vali = CIND2I(ker,n1,n2,T);
                mval = valr * valr + vali * vali;
                if (mval > mq) {
                    mq = mval;
                    qr = valr; qi = vali;
                }
            }
        }
        qr /= mq;
        qi = -qi / mq;
        // The clean loop
        for (int i=0; i < maxiter; i++) {
            nscore = 0;
            mmax = -1;
            stepr = (T) gain * (maxr * qr - maxi * qi);
            stepi = (T) gain * (maxr * qi + maxi * qr);
            CIND2R(mdl,argmax1,argmax2,T) += stepr;
            CIND2I(mdl,argmax1,argmax2,T) += stepi;
            // Take next step and compute score
            for (int n1=0; n1 < dim1; n1++) {
                wrap_n1 = (n1 + argmax1) % dim1;
                for (int n2=0; n2 < dim2; n2++) {
                    wrap_n2 = (n2 + argmax2) % dim2;
                    CIND2R(res,wrap_n1,wrap_n2,T) -= \
                      CIND2R(ker,n1,n2,T)*stepr - CIND2I(ker,n1,n2,T)*stepi;
                    CIND2I(res,wrap_n1,wrap_n2,T) -= \
                      CIND2R(ker,n1,n2,T)*stepi + CIND2I(ker,n1,n2,T)*stepr;
                    valr = CIND2R(res,wrap_n1,wrap_n2,T);
                    vali = CIND2I(res,wrap_n1,wrap_n2,T);
                    mval = valr * valr + vali * vali;
                    nscore += mval;
                    if (mval > mmax) {
                        nargmax1 = wrap_n1; nargmax2 = wrap_n2;
                        maxr = valr; maxi = vali;
                        mmax = mval;
                    }
                }
            }
            nscore = sqrt(nscore / (dim1 * dim2));
            if (firstscore < 0) firstscore = nscore;
            if (verb != 0)
                printf("Iter %d: Max=(%d,%d), Score = %f, Prev = %f\n", \
                    i, nargmax1, nargmax2, (double) (nscore/firstscore), \
                    (double) (score/firstscore));
            if (score > 0 && nscore > score) {
                // We've diverged: undo last step and give up
                CIND2R(mdl,argmax1,argmax2,T) -= stepr;
                CIND2I(mdl,argmax1,argmax2,T) -= stepi;
                for (int n1=0; n1 < dim1; n1++) {
                    wrap_n1 = (n1 + argmax1) % dim1;
                    for (int n2=0; n2 < dim2; n2++) {
                        wrap_n2 = (n2 + argmax2) % dim2;
                        CIND2R(res,wrap_n1,wrap_n2,T) += \
                          CIND2R(ker,n1,n2,T)*stepr - CIND2I(ker,n1,n2,T)*stepi;
                        CIND2I(res,wrap_n1,wrap_n2,T) += \
                          CIND2R(ker,n1,n2,T)*stepi + CIND2I(ker,n1,n2,T)*stepr;
                    }
                }
            } else if (score > 0 && (score - nscore) / firstscore < tol) {
                // We're done
                return i;
                return -i;
            }
            score = nscore;
            argmax1 = nargmax1; argmax2 = nargmax2;
        }
        return maxiter;
    }
    //   ____ _                  _     _      
    //  / ___| | ___  __ _ _ __ / | __| | ___ 
    // | |   | |/ _ \/ _` | '_ \| |/ _` |/ __|
    // | |___| |  __/ (_| | | | | | (_| | (__ 
    //  \____|_|\___|\__,_|_| |_|_|\__,_|\___|
    // Does a 1d complex-valued clean
    static int clean_1d_c(PyArrayObject *res, PyArrayObject *ker, 
            PyArrayObject *mdl, double gain, int maxiter, double tol,
            int verb) {
        T maxr=0, maxi=0, valr, vali, stepr, stepi, qr=0, qi=0;
        T score=-1, nscore, mmax, mval, mq=0;
        T firstscore=-1;
        int argmax=0, nargmax=0, dim=DIM(res,0), wrap_n;
        // Compute gain/phase of kernel
        for (int n=0; n < dim; n++) {
            valr = CIND1R(ker,n,T);
            vali = CIND1I(ker,n,T);
            mval = valr * valr + vali * vali;
            if (mval > mq) {
                mq = mval;
                qr = valr;
                qi = vali;
            }
        }
        qr /= mq;
        qi = -qi / mq;
        // The clean loop
        for (int i=0; i < maxiter; i++) {
            nscore = 0;
            mmax = -1;
            stepr = (T) gain * (maxr * qr - maxi * qi);
            stepi = (T) gain * (maxr * qi + maxi * qr);
            CIND1R(mdl,argmax,T) += stepr;
            CIND1I(mdl,argmax,T) += stepi;
            // Take next step and compute score
            for (int n=0; n < dim; n++) {
                wrap_n = (n + argmax) % dim;
                CIND1R(res,wrap_n,T) -= CIND1R(ker,n,T) * stepr - \
                                        CIND1I(ker,n,T) * stepi;
                CIND1I(res,wrap_n,T) -= CIND1R(ker,n,T) * stepi + \
                                        CIND1I(ker,n,T) * stepr;
                valr = CIND1R(res,wrap_n,T);
                vali = CIND1I(res,wrap_n,T);
                mval = valr * valr + vali * vali;
                nscore += mval;
                if (mval > mmax) {
                    nargmax = wrap_n;
                    maxr = valr;
                    maxi = vali;
                    mmax = mval;
                }
            }
            nscore = sqrt(nscore / dim);
            if (firstscore < 0) firstscore = nscore;
            if (verb != 0)
                printf("Iter %d: Max=(%d), Score = %f, Prev = %f\n", \
                    i, nargmax, (double) (nscore/firstscore), \
                    (double) (score/firstscore));
            if (score > 0 && nscore > score) {
                // We've diverged: undo last step and give up
                CIND1R(mdl,argmax,T) -= stepr;
                CIND1I(mdl,argmax,T) -= stepi;
                for (int n=0; n < dim; n++) {
                    wrap_n = (n + argmax) % dim;
                    CIND1R(res,wrap_n,T) += CIND1R(ker,n,T) * stepr - \
                                            CIND1I(ker,n,T) * stepi;
                    CIND1I(res,wrap_n,T) += CIND1R(ker,n,T) * stepi + \
                                            CIND1I(ker,n,T) * stepr;
                }
                return -i;
            } else if (score > 0 && (score - nscore) / firstscore < tol) {
                // We're done
                return i;
            }
            score = nscore;
            argmax = nargmax;
        }
        return maxiter;
    }
};  // END TEMPLATE

// __        __                               
// \ \      / / __ __ _ _ __  _ __   ___ _ __ 
//  \ \ /\ / / '__/ _` | '_ \| '_ \ / _ \ '__|
//   \ V  V /| | | (_| | |_) | |_) |  __/ |   
//    \_/\_/ |_|  \__,_| .__/| .__/ \___|_|   
//                     |_|   |_|              

// Clean wrapper that handles all different data types and dimensions
PyObject *clean(PyObject *self, PyObject *args, PyObject *kwargs) {
    PyArrayObject *res, *ker, *mdl;
    double gain=.1, tol=.001;
    int maxiter=200, rank=0, dim1, dim2, rv, verb;
    static char *kwlist[] = {"res", "ker", "mdl", "gain", \
                             "maxiter", "tol", "verbose", NULL};
    // Parse arguments and perform sanity check
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!O!O!|didi", kwlist, \
            &PyArray_Type, &res, &PyArray_Type, &ker, &PyArray_Type, &mdl, 
            &gain, &maxiter, &tol, &verb)) 
        return NULL;
    if (RANK(res) == 1) {
        rank = 1;
        CHK_ARRAY_RANK(ker, 1); CHK_ARRAY_RANK(mdl, 1);
        dim1 = DIM(res,0);
        CHK_ARRAY_DIM(ker, 0, dim1); CHK_ARRAY_DIM(mdl, 0, dim1);
    } else if (RANK(res) == 2) {
        rank = 2;
        CHK_ARRAY_RANK(ker, 2); CHK_ARRAY_RANK(mdl, 2);
        dim1 = DIM(res,0); dim2 = DIM(res,1);
        CHK_ARRAY_DIM(ker, 0, dim1); CHK_ARRAY_DIM(mdl, 0, dim1);
        CHK_ARRAY_DIM(ker, 1, dim2); CHK_ARRAY_DIM(mdl, 1, dim2);
    }
    if (TYPE(res) != TYPE(ker) || TYPE(res) != TYPE(mdl)) {
        PyErr_Format(PyExc_ValueError, "array types must match");
        return NULL;
    }
    Py_INCREF(res); Py_INCREF(ker); Py_INCREF(mdl);
    // Use template to implement data loops for all data types
    if (TYPE(res) == NPY_FLOAT) {
        if (rank == 1) {
            rv = Clean<float>::clean_1d_r(res,ker,mdl,gain,maxiter,tol,verb);
        } else {
            rv = Clean<float>::clean_2d_r(res,ker,mdl,gain,maxiter,tol,verb);
        }
    } else if (TYPE(res) == NPY_DOUBLE) {
        if (rank == 1) {
            rv = Clean<double>::clean_1d_r(res,ker,mdl,gain,maxiter,tol,verb);
        } else {
            rv = Clean<double>::clean_2d_r(res,ker,mdl,gain,maxiter,tol,verb);
        }
    } else if (TYPE(res) == NPY_LONGDOUBLE) {
        if (rank == 1) {
            rv = Clean<long double>::clean_1d_r(res,ker,mdl,gain,maxiter,tol,verb);
        } else {
            rv = Clean<long double>::clean_2d_r(res,ker,mdl,gain,maxiter,tol,verb);
        }
    } else if (TYPE(res) == NPY_CFLOAT) {
        if (rank == 1) {
            rv = Clean<float>::clean_1d_c(res,ker,mdl,gain,maxiter,tol,verb);
        } else {
            rv = Clean<float>::clean_2d_c(res,ker,mdl,gain,maxiter,tol,verb);
        }
    } else if (TYPE(res) == NPY_CDOUBLE) {
        if (rank == 1) {
            rv = Clean<double>::clean_1d_c(res,ker,mdl,gain,maxiter,tol,verb);
        } else {
            rv = Clean<double>::clean_2d_c(res,ker,mdl,gain,maxiter,tol,verb);
        }
    } else if (TYPE(res) == NPY_CLONGDOUBLE) {
        if (rank == 1) {
            rv = Clean<long double>::clean_1d_c(res,ker,mdl,gain,maxiter,tol,verb);
        } else {
            rv = Clean<long double>::clean_2d_c(res,ker,mdl,gain,maxiter,tol,verb);
        }
    } else {
        PyErr_Format(PyExc_ValueError, "Unsupported data type.");
        return NULL;
    }
    Py_DECREF(res); Py_DECREF(ker); Py_DECREF(mdl);
    return Py_BuildValue("i", rv);
}

// Wrap function into module
static PyMethodDef DeconvMethods[] = {
    {"clean", (PyCFunction)clean, METH_VARARGS|METH_KEYWORDS,
        "clean(res,ker,mdl,gain=.1,maxiter=200,tol=.001)\nPerform a 1 or 2 dimensional deconvolution using the CLEAN algorithm.."},
    {NULL, NULL}
};

PyMODINIT_FUNC init_deconv(void) {
    (void) Py_InitModule("_deconv", DeconvMethods);
    import_array();
};