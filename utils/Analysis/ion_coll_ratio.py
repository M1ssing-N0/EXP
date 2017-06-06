#!/usr/bin/python

# -*- Python -*-
# -*- coding: utf-8 -*-

"""Program to display the particle collision counts for each type
using diagnostic output from the CollideIon class in the
UserTreeDSMC module.

There are two simple routines here.  The main routine that parses the
input command line and a plotting/parsing routine.

Examples:

	$ python ion_coll_number -d 2 run2

Plots the collision count types for each ion type.

"""

import sys, getopt
import copy
import string
import numpy as np
import matplotlib.pyplot as pl
import scipy.interpolate as ip
import statsmodels.api as sm


def plot_data(filename, msz, line, dot, summary, izr, smooth, useTime, stride, trange):
    """Parse and plot the *.ION_coll output files generated by
    CollideIon

    Parameters:

    filename (string): is the input datafile name

    dot (bool): if True, markers set to dots

    summary(bool): if True, print summary data with not plots

    """

    # Marker type
    #
    mk = ''
    if line: mk += '-'
    if dot:  mk += '.'
    else:    mk += '-*'

    # Translation table to convert vertical bars and comments to spaces
    #
    trans = string.maketrans("#|", "  ")

    # Initialize data and header containers
    #
    tabl  = {}
    time  = []
    temp  = []
    etot  = []
    ncol  = 9
    head  = 2
    tail  = 2
    data  = {}

    # Species
    #
    spec5   = ['H', 'H+', 'He', 'He+', 'He++']
    spec1   = ['All']
    spec    = []
    nstanza = 0

    # Read and parse the file
    #
    file  = open(filename)
    for line in file:
        if line.find('Species')>=0:
            if line.find('(65535, 65535)')>=0:
                spec = spec1
            else:
                spec = spec5

            nstanza = len(spec)
            for v in spec: data[v] = {}
            
        if line.find('Time')>=0:    # Get the labels
            next = True
            labels = line.translate(trans).split()
            if line.find("W(") >= 0: 
                ncol = 13
            if line.find("N(nn)") >= 0: 
                ncol = 16
            if line.find("EratC") >= 0 or line.find("Efrac") >= 0: 
                tail = 12
        if line.find('[1]')>=0:     # Get the column indices
            toks = line.translate(trans).split()
            for i in range(head, len(toks)-tail):
                j = int(toks[i][1:-1]) - 1
                tabl[labels[j]] = i
                idx = (i-head) / ncol
                data[spec[idx]][labels[j]] = []
        if line.find('#')<0:        # Read the data lines
            toks = line.translate(trans).split()
            allZ = True             # Skip lines with zeros only
            for i in range(2,len(toks)):
                if float(toks[i])>0.0: 
                    allZ = False
                    break
            if not allZ:            
                # A non-zero line . . .  Make sure field counts are the
                # same (i.e. guard against the occasional badly written
                # output file
                if len(toks) == len(labels):
                    time.append(float(toks[ 0]))
                    temp.append(float(toks[ 1]))
                    if tail == 7:
                        etot.append(float(toks[-1]))
                    else:
                        etot.append(float(toks[-2]))
                    for i in range(head,len(toks)-tail):
                        idx = (i-head) / ncol
                        data[spec[idx]][labels[i]].append(float(toks[i]))
                else:
                    print "toks=", len(toks), " labels=", len(labels)

    # Scattering counts
    #
    scat = {}
    for v in spec:
        scat[v] = []
        for i in range(len(temp)):
            scat[v].append(data[v]['N(nn)'][i] + 
                           data[v]['N(ne)'][i] + 
                           data[v]['N(ie)'][i] )

    # Fields to plot
    #
    ekeys = ['N(ce)', 'N(ci)', 'N(ff)', 'N(rr)']
    elabs = ['collide', 'ionize', 'free-free', 'recomb']

    # Choose time or temp as xaxis
    #
    xaxis = temp
    if useTime: xaxis = time

    if not summary:
        icnt = 0
        flr  = 1.0e-10
        for k in range(0, 4):
            icnt += 1
            pl.subplot(2, 2, icnt)
            if useTime: pl.xlabel('Time')
            else:       pl.xlabel('Temperature')
            pl.ylabel('Ratio')
            for v in spec:
                ratio = []
                xx    = []
                for i in range(0, len(xaxis), stride):
                    if time[i] >= trange[0] and time[i] <= trange[1]:
                        if izr:
                            if scat[v][i] > 0.0 and data[v][ekeys[k]][i] > 0.0:
                                xx.append(xaxis[i])
                                ratio.append(data[v][ekeys[k]][i]/scat[v][i])
                        else:
                            xx.append(xaxis[i])
                            if scat[v][i] > 0.0: 
                                ratio.append(data[v][ekeys[k]][i]/scat[v][i])
                            else:
                                ratio.append(flr)

                if izr:
                    pl.semilogy(xx, ratio, mk, label=v, markersize=msz)
                elif smooth:
                    lws = sm.nonparametric.lowess(ratio, xx)
                    pl.semilogy(lws[:,0], lws[:,1], '-', label=v)
                else:
                    pl.semilogy(xx, ratio, mk, label=v, markersize=msz)
            pl.title(elabs[k])
            if icnt==4:
                leg = pl.legend(loc='best',borderpad=0,labelspacing=0)
                leg.get_title().set_fontsize('6')
                pl.setp(pl.gca().get_legend().get_texts(), fontsize='12')

        pl.get_current_fig_manager().full_screen_toggle()
        pl.show()
 
        # Inverse
        #
        for v in spec:
            pl.subplot(1, 1, 1)
            if useTime: pl.xlabel('Time')
            else:       pl.xlabel('Temperature')
            pl.ylabel('Ratio')
            for k in range(len(ekeys)):
                xx    = []
                ratio = []
                for i in range(0, len(xaxis), stride):
                    if time[i] < trange[0] or time[i] > trange[1]: continue
                    if izr:
                        if scat[v][i] > 0.0 and data[v][ekeys[k]][i] > 0.0: 
                            xx.append(xaxis[i])
                            ratio.append(data[v][ekeys[k]][i]/scat[v][i])
                    else:
                        xx.append(xaxis[i])
                        if scat[v][i] > 0.0 and data[v][ekeys[k]][i]: 
                            ratio.append(data[v][ekeys[k]][i]/scat[v][i])
                        else:
                            ratio.append(flr)

                if izr:
                    pl.semilogy(xx, ratio, mk, label=elabs[k], markersize=msz)
                elif smooth:
                    lws = sm.nonparametric.lowess(ratio, xx)
                    pl.semilogy(lws[:,0], lws[:,1], '-', label=elabs[k])
                else:
                    pl.semilogy(xx, ratio, mk, label=elabs[k], markersize=msz)
            pl.title(v)
            leg = pl.legend(loc='best',borderpad=0,labelspacing=0)
            leg.get_title().set_fontsize('6')
            pl.setp(pl.gca().get_legend().get_texts(), fontsize='12')
            pl.get_current_fig_manager().full_screen_toggle()
            pl.show()
 
        # Inverse
        #
        pl.subplot(1, 1, 1)
        if useTime: pl.xlabel('Time')
        else:       pl.xlabel('Temperature')
        pl.ylabel('Ratio')

        xx    = []
        ratio = []
        v = 'He+'
        s = 'N(ci)'
        for i in range(0, len(xaxis), stride):
            if time[i] >= trange[0] and time[i] <= trange[0]:
                if scat[v][i] > 0.0 and data[v][ekeys[k]][i]: 
                    xx.append(xaxis[i])
                    ratio.append(data[v][s][i]/scat[v][i])
                else:
                    xx.append(xaxis[i])
                    ratio.append(flr)
        if smooth:
            lws = sm.nonparametric.lowess(ratio, xx)
            pl.semilogy(lws[:,0], lws[:,1], '-', label='He+ ionize')
        else:
            pl.semilogy(xx, ratio, mk, label='He+ ionize', markersize=msz)

        xx    = []
        ratio = []
        v = 'He++'
        s = 'N(rr)'
        for i in range(0, len(xaxis), stride):
            if time[i] >= trange[0] and time[i] <= trange[1]:
                if scat[v][i] > 0.0 and data[v][ekeys[k]][i]: 
                    xx.append(xaxis[i])
                    ratio.append(data[v][s][i]/scat[v][i])
                else:
                    xx.append(xaxis[i])
                    ratio.append(flr)
        if smooth:
            lws = sm.nonparametric.lowess(ratio, xx)
            pl.semilogy(lws[:,0], lws[:,1], '-', label='He++ recomb')
        else:
            pl.semilogy(xx, ratio, mk, label='He++ recomb', markersize=msz)
    
        pl.title('He+/He++ equilibrium')
        leg = pl.legend(loc='best',borderpad=0,labelspacing=0)
        leg.get_title().set_fontsize('6')
        pl.setp(pl.gca().get_legend().get_texts(), fontsize='12')
        pl.get_current_fig_manager().full_screen_toggle()
        pl.show()
 
    if not summary:
        flr  = 1.0e-10
        pl.subplot(1, 1, 1)
        if useTime: pl.xlabel('Time')
        else:       pl.xlabel('Temperature')
        pl.ylabel('Counts')
        for v in spec:
            xx = []
            yy = []
            for i in range(0, len(xaxis), stride):
                if time[i] >= trange[0] and time[i] <= trange[1]:
                    if izr:
                        if scat[v][i] > 0.0:
                            xx.append(xaxis[i])
                            yy.append(scat[v][i])
                        else:
                            xx.append(xaxis[i])
                            if scat[v][i] > 0.0: 
                                ratio.append(scat[v][i])
                            else:
                                ratio.append(flr)

            if smooth:
                lws = sm.nonparametric.lowess(yy, xx)
                pl.semilogy(lws[:,0], lws[:,1], '-', label=v)
            else:
                pl.semilogy(xx, yy, mk, label=v, markersize=msz)
        leg = pl.legend(loc='best',borderpad=0,labelspacing=0)
        leg.get_title().set_fontsize('6')
        pl.title('Scattering counts')
        pl.setp(pl.gca().get_legend().get_texts(), fontsize='12')
        pl.get_current_fig_manager().full_screen_toggle()
        pl.show()

    #
    # Summary data
    #
    H_scat    = sum(scat['H'])
    Hp_scat   = sum(scat['H+'])
    Hep_scat  = sum(scat['He+'])
    Hepp_scat = sum(scat['He++'])

    H_ionz    = sum(data['H'   ]['N(ci)'])
    Hp_rcmb   = sum(data['H+'  ]['N(rr)'])
    Hep_ionz  = sum(data['He+' ]['N(ci)'])
    Hepp_rcmb = sum(data['He++']['N(rr)'])
    
    print("--------------------------------")
    print("Ionization/recombination summary")
    print("--------------------------------")

    if H_scat > 0:
        print("N(H,    ionz) = {:13g} {:13g} {:13g}".format(H_scat, H_ionz, H_ionz/H_scat))
    else:
        print("N(H,    ionz) = {:13g} {:13g} {:<13s}".format(H_scat, H_ionz, "inf"))

    if Hp_scat > 0:
        print("N(H+,   rcmb) = {:13g} {:13g} {:13g}".format(Hp_scat, Hp_rcmb, Hp_rcmb/Hp_scat))
    else:
        print("N(H+,   rcmb) = {:13g} {:13g} {:<13s}".format(Hp_scat, Hp_rcmb, "inf"))

    if Hep_scat > 0:
        print("N(He+,  ionz) = {:13g} {:13g} {:13g}".format(Hep_scat, Hep_ionz, Hep_ionz/Hep_scat))
    else:
        print("N(He+,  ionz) = {:13g} {:13g} {:<13s}".format(Hep_scat, Hep_ionz, "inf"))

    if Hepp_scat > 0:
        print("N(He++, rcmb) = {:13g} {:13g} {:13g}".format(Hepp_scat, Hepp_rcmb, Hepp_rcmb/Hepp_scat))
    else:
        print("N(He++, rcmb) = {:13g} {:13g} {:<13s}".format(Hepp_scat, Hepp_rcmb, "inf"))
    
def main(argv):
    """ Parse the command line and call the parsing and plotting routine """

    lin = False
    dot = False
    msz = 4
    rpt = False
    izr = False
    lss = False
    tim = False
    str = 1
    trange = [0.0, 1.0e20]

    usage = '[-p | --point | -m <size> | --msize=<size> | -s | --summary | -L | --lowess | --line | --stride=<int> | --tmin=<float> | --tmax=<float>] <runtag>'

    try:
        opts, args = getopt.getopt(argv,"hm:pszL", ["help", "msize=", "point", "summary", "ignore", "lowess", "line", "time", "stride=", "tmin=", "tmax="])
    except getopt.GetoptError:
        print 'Syntax Error'
        print sys.argv[0], usage
        sys.exit(2)
    for opt, arg in opts:
        if opt in ("-h", "--help"):
            print sys.argv[0], usage
            sys.exit()
        elif opt in ("-p", "--point"):
            dot = True
        elif opt in ("-m", "--msize"):
            msz = int(arg)
        elif opt in ("-s", "--summary"):
            rpt = True
        elif opt in ("-z", "--ignore"):
            izr = True
        elif opt in ("-L", "--lowess"):
            lss = True
        elif opt in ("--line"):
            lin = True
        elif opt in ("--time"):
            tim = True
        elif opt in ("--stride"):
            str = int(arg)
        elif opt in ("--tmin"):
            trange[0] = float(arg)
        elif opt in ("--tmax"):
            trange[1] = float(arg)

    suffix = ".ION_coll"
    if len(args)>0:
        filename = args[0] + suffix;
    else:
        filename = "run" + suffix;

    plot_data(filename, msz, lin, dot, rpt, izr, lss, tim, str, trange)

if __name__ == "__main__":
   main(sys.argv[1:])
