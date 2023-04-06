

\#####################################################################
<br>\# CS:Malloc Lab
<br>\# Handout files for students
<br>\#
<br>\# Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
<br>\# May not be used, modified, or copied without permission.
<br>\#
<br>\######################################################################

***********
Main Files:
***********

mm.c
<br>Your solution malloc package. mm.c is a file that
    you will be handing in, and is the only file you should modify.

mdriver.c	
<br>The malloc driver that tests your mm.c files.

short{1,2}-bal.rep
<br>Two tiny tracefiles to help you get started. 

Makefile	
<br>Builds the driver

**********************************
Other support files for the driver
**********************************

config.h	Configures the malloc lab driver
<br>fsecs.{c,h}	Wrapper function for the different timer packages
<br>clock.{c,h}	Routines for accessing the Pentium and Alpha cycle counters
<br>fcyc.{c,h}	Timer functions based on cycle counters
<br>ftimer.{c,h}	Timer functions based on interval timers and gettimeofday()


memlib.{c,h}	Models the heap and sbrk function

*******************************
Building and running the driver
*******************************
To build the driver, type "make" to the shell.

To run the driver on a tiny test trace:
<br>unix> mdriver -V -f short1-bal.rep

The -V option prints out helpful tracing and summary information.

To get a list of the driver flags:
<br>unix> mdriver -h
	
![malloclab-readme_page-0001](https://user-images.githubusercontent.com/37990408/230283333-d64bc688-5c42-4232-8841-5a4a280fd852.jpg)
![malloclab-readme_page-0002](https://user-images.githubusercontent.com/37990408/230283316-7b635415-80f8-419d-b42a-9e00ed7a896b.jpg)
![malloclab-readme_page-0003](https://user-images.githubusercontent.com/37990408/230283320-1b3f3963-a881-481c-87b5-2e6fcb72b2c8.jpg)
![malloclab-readme_page-0004](https://user-images.githubusercontent.com/37990408/230283323-e78271f5-9732-45c9-bfaa-09d0d73775f6.jpg)
![malloclab-readme_page-0005](https://user-images.githubusercontent.com/37990408/230283328-fdd77df3-2237-4608-91db-1ff122dd7d7c.jpg)
![malloclab-readme_page-0006](https://user-images.githubusercontent.com/37990408/230283331-d77fd9a6-39ef-4a84-907f-de5976d2db6e.jpg)


