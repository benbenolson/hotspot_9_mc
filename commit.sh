#!/bin/bash

# To get my custom-compiled newer version of git
export PATH="/home/molson5/.local/bin:$PATH"

git add *.cpp *.hpp *.java *.h *.c *.sh
git commit -a
git push
