ifneq ($(strip $(findstring i386-linux,$(tgt_arch)) \
               $(findstring x86_64-linux,$(tgt_arch))),)
# List of packages (low level first)
packages := test dev devtest config mon monobs control
endif

ifneq ($(findstring ppc-rtems-rce,$(tgt_arch)),)
# List of packages (low level first)
packages := 

endif
