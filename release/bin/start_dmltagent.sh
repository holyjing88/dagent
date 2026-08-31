#!/bin/sh
cd $(dirname $0)
./dmltagent --tlogconf=../cfg/tagent_log.xml --conf-file=../cfg/tagent.xml --log-file=../log/dmltagent -D start
