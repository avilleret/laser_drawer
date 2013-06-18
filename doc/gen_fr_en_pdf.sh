#!/bin/bash
_DIR=$(dirname $(readlink -f $0))
cd "$_DIR" # This is the directory where this file is (should be same as .tex files)
_NAME=laser_drawer
rtn2=0
rtn=0

echo -n Compile la version française... 
pdflatex -synctex=1 -jobname=$_NAME -interaction=nonstopmode main-fr.tex > log-fr
rtn=$?
if [ $rtn -eq 0 ] 
then 
   echo OK
else
   error
   exit -1
fi

echo -n Compresse la version française... 
gs -dBATCH -dNOPAUSE -q -sDEVICE=pdfwrite -sOutputFile=$_NAME-web.pdf $_NAME.pdf
rtn=$?
if [ $rtn -eq 0 ] 
then 
   echo OK
else
   error
   exit -1
fi

echo -n Compile la version anglaise... 
pdflatex -synctex=1 -jobname=$_NAME-en -interaction=nonstopmode main-en.tex > log-en
rtn=$?
if [ $rtn -eq 0 ] 
then 
   echo OK
else
   error
   exit -1
fi

echo -n Compresse la version anglaise... 
gs -dBATCH -dNOPAUSE -q -sDEVICE=pdfwrite -sOutputFile=$_NAME-en-web.pdf $_NAME-en.pdf
rtn=$?
if [ $rtn -eq 0 ] 
then 
   echo OK
else
   error
   exit -1
fi

exit 0
#~ exit $(($rtn+$rtn2))
