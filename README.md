# gateworks-sample-apps #

This project consists of several sample applications written by Gateworks Corporation in order to help our customers learn certain concepts and provide real world examples. Most of these sample applications are to be used specifically with our products, though they can probably be adapted for use in other systems.

## Compatibility ##

These programs are made in mind to run on Gateworks Corporation products.

## Compilation ##

Most of these individual projects should provide a `Makefile`. Thus, if target compiling or if a terminal is configured properly, a simple `make` is all that should be required after changing directories into the specified project.

## Cross Compilation ##

To cross compile for a target board, please verify that the toolchain you're using is for the intended platform. For example, to compile for the i.mx6 platform, you could use the [SDK build by our Yocto BSP](http://trac.gateworks.com/wiki/Yocto/SDK).
