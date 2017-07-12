CPPFLAGS += -D_ACQIRIS -D_LINUX

libnames    := simframe simmovie simtimetool playframe acqsim epixsim

libsrcs_simframe := SimFrame.cc
liblibs_simframe := pdsdata/xtcdata pdsdata/psddl_pdsdata pdsdata/compressdata 
liblibs_simframe += pds/service pds/xtc pds/collection pds/mon pds/vmon pds/utility pds/client 
libincs_simframe := pdsdata/include ndarray/include boost/include 

libsrcs_simmovie := SimMovie.cc
liblibs_simmovie := pdsdata/xtcdata pdsdata/psddl_pdsdata pdsdata/compressdata 
liblibs_simmovie += pds/service pds/xtc pds/collection pds/mon pds/vmon pds/utility pds/client 
libincs_simmovie := pdsdata/include ndarray/include boost/include 

libsrcs_simtimetool := SimTimeTool.cc
liblibs_simtimetool := pdsdata/xtcdata pdsdata/psddl_pdsdata pdsdata/compressdata 
liblibs_simtimetool += pds/service pds/xtc pds/collection pds/mon pds/vmon pds/utility pds/client 
libincs_simtimetool := pdsdata/include ndarray/include boost/include 

libsrcs_playframe := PlayFrame.cc
liblibs_playframe := pdsdata/xtcdata pdsdata/psddl_pdsdata pdsdata/compressdata 
liblibs_playframe += pds/service pds/xtc pds/collection pds/mon pds/vmon pds/utility pds/client 
libincs_playframe := pdsdata/include ndarray/include boost/include 

libsrcs_acqsim := AcqWriter.cc
liblibs_acqsim := pdsdata/xtcdata pdsdata/psddl_pdsdata pdsdata/compressdata 
liblibs_acqsim += pds/service pds/xtc pds/collection pds/mon pds/vmon pds/utility pds/client 
libincs_acqsim := pdsdata/include ndarray/include boost/include 

libsrcs_epixsim := EpixWriter.cc
liblibs_epixsim := pdsdata/xtcdata pdsdata/psddl_pdsdata pdsdata/compressdata 
liblibs_epixsim += pds/service pds/xtc pds/collection pds/mon pds/vmon pds/utility pds/client 
libincs_epixsim := pdsdata/include ndarray/include boost/include 

tgtnames    := evgr evg pnccdwriter xtctruncate pnccdreader dsstest xcasttest xtccompress pgpwidget pnccdwidget xtccamfix compressstat epixwriter microspin xtcwriter  epix100abintoxtc

tgtsrcs_evrobs := evrobs.cc
tgtincs_evrobs := evgr
tgtlibs_evrobs := pdsdata/xtcdata evgr/evr evgr/evg pds/service pds/collection pds/xtc pds/utility pds/management pds/client pds/evgr 
tgtslib_evrobs := $(USRLIB)/rt

tgtsrcs_evg := evg.cc
tgtincs_evg := evg
tgtlibs_evg := pdsdata/xtcdata pdsdata/psddl_pdsdata evgr/evg evgr/evr pds/service pds/collection pds/config pds/xtc pds/mon pds/vmon pds/utility pds/management pds/client pds/evgr pds/configdata pds/configdbc pds/confignfs pds/configsql
tgtslib_evg := $(USRLIBDIR)/rt $(USRLIBDIR)/mysql/mysqlclient

tgtsrcs_evgr := evgr.cc
tgtincs_evgr := evgr
tgtlibs_evgr := pdsdata/xtcdata pdsdata/psddl_pdsdata evgr/evg evgr/evr pds/service pds/collection pds/config pds/xtc pds/mon pds/vmon pds/utility pds/management pds/client pds/evgr pds/configdata pds/configdbc pds/confignfs pds/configsql
tgtslib_evgr := $(USRLIBDIR)/rt $(USRLIBDIR)/mysql/mysqlclient

tgtsrcs_evgrd := evgrd.cc
tgtincs_evgrd := evgr
tgtlibs_evgrd := pdsdata/xtcdata pdsdata/psddl_pdsdata evgr/evg evgr/evr pds/service pds/collection pds/config pds/xtc pds/mon pds/vmon pds/utility pds/management pds/client pds/evgr
tgtslib_evgrd := $(USRLIB)/rt

tgtsrcs_xtcwriter := xtcwriter.cc
tgtlibs_xtcwriter := pdsdata/xtcdata pdsdata/psddl_pdsdata pds/service pds/xtc pds/collection pds/mon pds/vmon pds/utility
tgtslib_xtcwriter := $(USRLIB)/rt $(USRLIB)/dl
tgtincs_xtcwriter := pdsdata/include ndarray/include boost/include 

tgtsrcs_epixwriter := epixwriter.cc
tgtlibs_epixwriter := pdsdata/xtcdata pdsdata/psddl_pdsdata pds/service pds/xtc pds/collection pds/mon pds/vmon pds/utility
tgtslib_epixwriter := $(USRLIB)/rt
tgtincs_epixwriter := pdsdata/include ndarray/include boost/include

tgtsrcs_pnccdreader := pnccdreader.cc
tgtlibs_pnccdreader := pdsdata/psddl_pdsdata pdsdata/xtcdata pds/service
tgtslib_pnccdreader := $(USRLIB)/rt
tgtincs_pnccdreader := pdsdata/include ndarray/include boost/include 

tgtsrcs_pnccdwriter := pnccd.cc
tgtlibs_pnccdwriter := pdsdata/xtcdata pds/service
tgtslib_pnccdwriter := $(USRLIB)/rt
tgtincs_pnccdwriter := pdsdata/include ndarray/include boost/include 

tgtsrcs_xtctruncate := xtctruncate.cc
tgtlibs_xtctruncate := pdsdata/xtcdata pds/service
tgtslib_xtctruncate := $(USRLIB)/rt
tgtincs_xtctruncate := pdsdata/include ndarray/include boost/include 

tgtsrcs_dsstest := dsstest.cc
tgtlibs_dsstest := pds/service pdsdata/xtcdata
tgtslib_dsstest := $(USRLIB)/rt

tgtsrcs_xcasttest := xcasttest.cc
tgtslib_xcasttest := $(USRLIB)/rt

tgtsrcs_acltest := acltest.cc
tgtslib_acltest := $(USRLIB)/rt $(USRLIB)/acl

tgtsrcs_xcasttest := xcasttest.cc
tgtslib_xcasttest := $(USRLIB)/rt

tgtsrcs_microspin := microspin.cc
tgtslib_microspin := $(USRLIB)/rt

tgtsrcs_xtccompress := xtccompress.cc
tgtlibs_xtccompress := pdsdata/xtcdata pdsdata/psddl_pdsdata pdsdata/compressdata 
tgtlibs_xtccompress += pds/service pds/xtc pds/collection pds/mon pds/vmon pds/utility pds/client 
tgtlibs_xtccompress += pds/clientcompress pds/pnccdFrameV0 pds/vmon pds/management
tgtslib_xtccompress := ${USRLIBDIR}/rt ${USRLIBDIR}/pthread 
tgtincs_xtccompress := pdsdata/include ndarray/include boost/include 

tgtsrcs_pgpwidget := pgpWidget.cc
tgtlibs_pgpwidget := pds/pgp pdsapp/padmon pdsdata/xtcdata pdsdata/appdata pdsdata/psddl_pdsdata
tgtslib_pgpwidget := $(USRLIB)/rt
tgtincs_pgpwidget := pdsdata/include ndarray/include boost/include

tgtsrcs_pnccdwidget := pnccdWidget.cc
tgtlibs_pnccdwidget := pds/pgp
tgtslib_pnccdwidget := $(USRLIB)/rt
tgtincs_pnccdwidget := pdsdata/include 

tgtsrcs_fccdwidget := fccdWidget.cc
tgtslib_fccdwidget := $(USRLIB)/rt
tgtincs_fccdwidget := pdsdata/include 

tgtsrcs_xtccamfix := xtccamfix.cc
tgtlibs_xtccamfix := pdsdata/xtcdata
tgtslib_xtccamfix := ${USRLIBDIR}/rt
tgtincs_xtccamfix := pdsdata/include

tgtsrcs_compressstat := compressstat.cc
tgtlibs_compressstat := pds/service pdsdata/xtcdata pdsdata/compressdata pdsdata/anadata pdsdata/indexdata
tgtslib_compressstat := ${USRLIBDIR}/rt ${USRLIBDIR}/pthread 
tgtincs_compressstat := pdsdata/include boost/include

tgtsrcs_fccdmonserver := fccdmonserver.cc
tgtlibs_fccdmonserver := pds/service pds/collection pds/xtc pds/mon pds/vmon pds/utility pds/management pds/client pds/udpcam pds/config pds/configdbc pds/confignfs pds/configsql offlinedb/mysqlclient
tgtlibs_fccdmonserver += pdsdata/xtcdata pdsdata/compressdata pdsdata/appdata pdsdata/psddl_pdsdata
tgtslib_fccdmonserver := ${USRLIBDIR}/rt
tgtincs_fccdmonserver := pdsdata/include boost/include ndarray/include

tgtsrcs_buffer := buffer.cc
tgtslib_buffer := ${USRLIBDIR}/rt

tgtsrcs_epixbintoxtc := epixbintoxtc.cc
tgtlibs_epixbintoxtc := pds/service pds/collection pds/xtc pds/mon pds/vmon pds/utility pds/management pds/client pds/udpcam pds/config pds/configdbc pds/confignfs pds/configsql offlinedb/mysqlclient
tgtlibs_epixbintoxtc += pdsdata/xtcdata pdsdata/compressdata pdsdata/psddl_pdsdata
tgtslib_epixbintoxtc := ${USRLIBDIR}/rt
tgtincs_epixbintoxtc := pdsdata/include boost/include ndarray/include

tgtsrcs_epix10kbintoxtc := epix10kbintoxtc.cc
tgtlibs_epix10kbintoxtc := pds/service pds/collection pds/xtc pds/mon pds/vmon pds/utility pds/management pds/client pds/udpcam pds/config pds/configdbc pds/confignfs pds/configsql offlinedb/mysqlclient
tgtlibs_epix10kbintoxtc += pdsdata/xtcdata pdsdata/compressdata pdsdata/psddl_pdsdata
tgtslib_epix10kbintoxtc := ${USRLIBDIR}/rt
tgtincs_epix10kbintoxtc := pdsdata/include boost/include ndarray/include

tgtsrcs_epix100abintoxtc := epix100abintoxtc.cc
tgtlibs_epix100abintoxtc := pds/service pds/collection pds/xtc pds/mon pds/vmon pds/utility pds/management pds/client pds/udpcam pds/config pds/configdbc pds/confignfs pds/configsql offlinedb/mysqlclient
tgtlibs_epix100abintoxtc += pdsdata/xtcdata pdsdata/compressdata pdsdata/psddl_pdsdata
tgtslib_epix100abintoxtc := ${USRLIBDIR}/rt
tgtincs_epix100abintoxtc := pdsdata/include boost/include ndarray/include

tgtsrcs_netlink := netlink.cc
tgtlibs_netlink := pds/collection pds/service pdsdata/xtcdata
tgtslib_netlink := ${USRLIBDIR}/rt
tgtincs_netlink := 

libnames :=
#tgtnames := tasktest xcasttest quadadc quadadc_dma amctiming
#tgtnames := tasktest xcasttest quadadc quadadc_dma quadadc_mon quadadc_cal amctiming amcmonitor tprca
tgtnames := tasktest xcasttest amctiming amcmonitor tprca xpm_simple dti_simple

tgtsrcs_tasktest := tasktest.cc
tgtlibs_tasktest := pds/service pdsdata/xtcdata
tgtslib_tasktest := $(USRLIBDIR)/rt

tgtsrcs_quadadc := quadadc.cc
tgtlibs_quadadc := pds/quadadc pds/tpr
tgtincs_quadadc := evgr
tgtslib_quadadc := $(USRLIBDIR)/rt
tgtlibs_quadadc += pds/epicstools epics/ca epics/Com
tgtincs_quadadc += epics/include epics/include/os/Linux

tgtsrcs_quadadc_dma := quadadc_dma.cc
tgtlibs_quadadc_dma := pds/quadadc pds/tprdsbase pds/tpr pds/service pdsdata/xtcdata
tgtincs_quadadc_dma := evgr
tgtslib_quadadc_dma := $(USRLIBDIR)/rt pthread
tgtlibs_quadadc_dma += pds/epicstools epics/ca epics/Com
tgtincs_quadadc_dma += epics/include epics/include/os/Linux

tgtsrcs_quadadc_cal := quadadc_cal.cc
tgtlibs_quadadc_cal := pds/quadadc pds/tprdsbase pds/tpr pds/service pdsdata/xtcdata
tgtincs_quadadc_cal := evgr
tgtslib_quadadc_cal := $(USRLIBDIR)/rt pthread
tgtlibs_quadadc_cal := pds/quadadc pds/tprdsbase pds/tpr pds/service 
tgtlibs_quadadc_cal += pdsdata/xtcdata pdsdata/psddl_pdsdata pdsapp/padmon pdsdata/appdata
tgtlibs_quadadc_cal += pds/epicstools epics/ca epics/Com
tgtincs_quadadc_cal := pdsdata/include ndarray/include boost/include evgr
tgtincs_quadadc_cal += epics/include epics/include/os/Linux

tgtsrcs_quadadc_mon := quadadc_mon.cc
tgtlibs_quadadc_mon := pds/service 
#tgtlibs_quadadc_mon += pds/quadadc pds/tprdsbaseb pds/tpr
tgtlibs_quadadc_mon += hsd/hsd
tgtlibs_quadadc_mon += pdsdata/xtcdata pdsdata/psddl_pdsdata pdsapp/padmon pdsdata/appdata
tgtincs_quadadc_mon := pdsdata/include ndarray/include boost/include evgr
tgtslib_quadadc_mon := $(USRLIBDIR)/rt pthread
tgtlibs_quadadc_mon += pds/epicstools epics/ca epics/Com
tgtincs_quadadc_mon += epics/include epics/include/os/Linux hsd/include

tgtsrcs_quadadc_reset := quadadc_reset.cc
tgtlibs_quadadc_reset := pds/quadadc pds/tpr
tgtincs_quadadc_reset := evgr
tgtslib_quadadc_reset := $(USRLIBDIR)/rt
tgtlibs_quadadc_reset += pds/epicstools epics/ca epics/Com
tgtincs_quadadc_reset += epics/include epics/include/os/Linux

tgtsrcs_amctiming := amctiming.cc
tgtlibs_amctiming := pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtslib_amctiming := dl pthread rt

tgtsrcs_amcmonitor := amcmonitor.cc
tgtlibs_amcmonitor := pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtslib_amcmonitor := dl pthread rt

#tgtnames += tprsh tprca tprbsaca tprutil

tgtsrcs_tprutil := tprutil.cc
tgtlibs_tprutil := pds/tpr pds/service pdsdata/xtcdata
tgtslib_tprutil := ${USRLIBDIR}/rt pthread
tgtincs_tprutil := evgr

tgtsrcs_tprsh := tprsh.cc
tgtlibs_tprsh := pds/collection pds/service pdsdata/xtcdata evgr/evr
tgtslib_tprsh := ${USRLIBDIR}/rt pthread
tgtincs_tprsh := evgr

tgtsrcs_tprca := tprca.cc
tgtlibs_tprca := pds/collection pds/service pdsdata/xtcdata evgr/evr
tgtlibs_tprca += pds/epicstools epics/ca epics/Com
tgtslib_tprca := ${USRLIBDIR}/rt
tgtincs_tprca := evgr
tgtincs_tprca += epics/include epics/include/os/Linux

tgtsrcs_tprbsaca := tprbsaca.cc
tgtlibs_tprbsaca := pds/collection pds/service pdsdata/xtcdata evgr/evr
tgtlibs_tprbsaca += pds/epicstools epics/ca epics/Com
tgtslib_tprbsaca := ${USRLIBDIR}/rt
tgtincs_tprbsaca := evgr
tgtincs_tprbsaca += epics/include epics/include/os/Linux

#AMCBSADIR := /reg/neh/home/weaver/amc-bsa
AMCBSADIR := /afs/slac/u/ec/weaver/vol1/BSA/master

tgtsrcs_tpgbsaca := tpgbsaca.cc
tgtlibs_tpgbsaca := pds/collection pds/service pdsdata/xtcdata evgr/evr
tgtlibs_tpgbsaca += pds/epicstools epics/ca epics/Com
tgtlibs_tpgbsaca += pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtslib_tpgbsaca := ${USRLIBDIR}/rt ${AMCBSADIR}/lib/linux-x86_64/bsa
tgtincs_tpgbsaca := evgr
tgtincs_tpgbsaca += epics/include epics/include/os/Linux
tgtsinc_tpgbsaca := ${AMCBSADIR}/include

tgtsrcs_acebsaca := acebsaca.cc
tgtlibs_acebsaca := pds/collection pds/service pdsdata/xtcdata evgr/evr
tgtlibs_acebsaca += pds/epicstools epics/ca epics/Com
tgtlibs_acebsaca += pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtslib_acebsaca := ${USRLIBDIR}/rt ${AMCBSADIR}/lib/linux-x86_64/bsa
tgtincs_acebsaca := evgr
tgtincs_acebsaca += epics/include epics/include/os/Linux
tgtsinc_acebsaca := ${AMCBSADIR}/include

tgtsrcs_bsaapp := bsaapp.cc
tgtlibs_bsaapp := pds/collection pds/service pdsdata/xtcdata evgr/evr
tgtlibs_bsaapp += pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtslib_bsaapp := ${USRLIBDIR}/rt ${AMCBSADIR}/lib/linux-x86_64/bsa
tgtincs_bsaapp := evgr
tgtsinc_bsaapp := ${AMCBSADIR}/include

tgtsrcs_dtimon := dtimon.cc
tgtincs_dtimon := cpsw/include cpsw_boost/include yaml/include
tgtincs_dtimon += epics/include epics/include/os/Linux
tgtlibs_dtimon := pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtlibs_dtimon += pds/epicstools epics/ca epics/Com
tgtslib_dtimon := rt pthread

#tgtnames := dtimon

tgtsrcs_dtiprbs := dtiprbs.cc
tgtincs_dtiprbs := cpsw/include cpsw_boost/include yaml/include
tgtincs_dtiprbs += epics/include epics/include/os/Linux
tgtincs_dtiprbs += cpsw/include
tgtlibs_dtiprbs := pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtlibs_dtiprbs += pds/epicstools epics/ca epics/Com
tgtslib_dtiprbs := rt pthread

#tgtnames := dtiprbs

tgtsrcs_dtisim := dtisim.cc
tgtincs_dtisim := cpsw/include cpsw_boost/include yaml/include
tgtincs_dtisim += epics/include epics/include/os/Linux
tgtincs_dtisim += cpsw/include
tgtlibs_dtisim := pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtlibs_dtisim += pds/epicstools epics/ca epics/Com
tgtslib_dtisim := rt pthread

#tgtnames := dtisim

tgtsrcs_timdly := timdly.cc
tgtincs_timdly := cpsw/include cpsw_boost/include yaml/include
tgtincs_timdly += epics/include epics/include/os/Linux
tgtincs_timdly += cpsw/include
tgtlibs_timdly := pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtlibs_timdly += pds/epicstools epics/ca epics/Com
tgtslib_timdly := rt pthread

#tgtnames := timdly

tgtsrcs_xpm_simple := xpm_simple.cc
tgtincs_xpm_simple := cpsw/include cpsw_boost/include yaml/include
tgtlibs_xpm_simple := pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtslib_xpm_simple := rt dl

#tgtnames := xpm_simple

tgtsrcs_dti_simple := dti_simple.cc
tgtincs_dti_simple := cpsw/include cpsw_boost/include yaml/include
tgtlibs_dti_simple := pds/cphw cpsw/cpsw yaml/yaml-cpp
tgtslib_dti_simple := rt dl

#tgtnames := dti_simple
