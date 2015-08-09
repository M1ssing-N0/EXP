#!/usr/bin/python -i

"""Module with predefined plotting and targets for pspHistoE output

The main functions begin with make* function.  After make* (or a
readDB() which is called by a make*), you may use showlabs() to see
the name tags for all available fields.

   readDB(file)            : read pspHistoE output file named "file"

   showlabs()              : show the fields available for plotting

   makeM(file, labs, temp) : plot fields in array "labs" from pspHistoE "file"
                             assuming temperature "temp"

   makeEs(file, temp)      : plot ion and electron energies from pspHistoE 
                             "file" assuming temperature "temp"

   makeEe(file, temp)      : plot electron energies per specie from pspHistoE 
                             "file" assuming temperature "temp"

   makeEi(file, temp)      : plot ion energies per specie from pspHistoE 
                             "file" assuming temperature "temp"

   makeEH(file, temp)      : plot ion and electron energies for H from 
                             pspHistoE "file" assuming temperature "temp"

   makeEHe(file, temp)     : plot ion and electron energies for He from 
                             pspHistoE "file" assuming temperature "temp"

"""

import matplotlib.pyplot as plt
import numpy as np
import os, sys
import astroval as C

flab = []

def readDB(infile):
    global flab

    flab = []

    file = open(infile)
    data = []
    line = file.readline()  # Label line
    flab = [v for v in line[1:].split()]
    llab = len(flab)
    line = file.readline()  # Separators
    for line in file:
        t = line.split()
        if len(t) == llab and t[1] != 'Overflow':
            data.append([float(v) for v in t])

    a = np.array(data).transpose()

    db = {}
    icnt = 0
    for l in flab:
        db[l] = a[icnt]
        icnt += 1

    return db

def showLabs():
    icnt = 0
    for v in flab:
        icnt += 1
        print "{:10s}".format(v),
        if icnt % 6 == 0: print


def makeM(infile,labs, T):
    db = readDB(infile)

    for lab in labs:
        if lab not in flab:
            print "No such field, available data is:"
            showLabs()
            return

    ymax = 1.0e-20
    for lab in labs:
        ym = max(db[lab])
        if ym>0:
            plt.semilogy(db['Energy'], db[lab], '-', label=lab)
            ymax = max(ymax, ym)
    E = ymax*np.sqrt(db['Energy'])*np.exp(-db['Energy']/(C.k_B*T/C.ev))
    plt.semilogy(db['Energy'], E, '-', label='Expected')
    plt.legend()
    plt.xlabel('Energy')
    plt.ylabel('Counts')
    plt.show()

def makeEs(infile,T):
    labs = ['Total_i','Total_e']
    makeM(infile, labs, T)

def makeEe(infile,T):
    labs = ['(1,1)_e', '(1,2)_e', '(2,1)_e', '(2,2)_e', '(2,3)_e']
    makeM(infile, labs, T)

def makeEi(infile,T):
    labs = ['(1,1)_i', '(1,2)_i', '(2,1)_i', '(2,2)_i', '(2,3)_i']
    makeM(infile, labs, T)

def makeEH(infile,T):
    labs = ['(1,1)_e', '(1,2)_e', '(1,1)_i', '(1,2)_i']
    makeM(infile, labs, T)

def makeEHe(infile,T):
    labs = ['(2,1)_e', '(2,2)_e', '(2,3)_e', '(2,1)_i', '(2,2)_i', '(2,3)_i']
    makeM(infile, labs, T)