#!/bin/bash

while pgrep -x supermercato >/dev/null; do sleep 1s; done

if [ -s "resoconto.log" ]; then
	exec 3<resoconto.log
	while read -u 3 linea ; do
		echo $linea
	done
else
	echo "Errore in $0" 1>&2
fi