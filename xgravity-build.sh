#!/bin/bash

gcc xgravity.c -o xgravity -lm -lX11 -lpthread
gcc xgravity.c -o xgravity-64 -lm -lX11 -m64 -lpthread
gcc xgravity.c -o xgravity-32 -lm -lX11 -m32 -lpthread
