Symreg tool
===========

This project contains the symreg tool.

The symreg tool can read and write to SoC registers using symbolic names.
It needs access to the register using debugfs so this interface must be enabled in the switch driver.

Building
========

You can initialize the projects build folder with cmake and configure the project with ccmake and then build the
executables with cmake:


```
$ cmake -B build
$ ccmake -B build
$ cmake --build build
```

You will need to select for which SoC to build the tool by enabling the SoC in the configuration (using ccmake).

The executables can be found in the build folder
