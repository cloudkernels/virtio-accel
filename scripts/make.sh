#!/bin/sh

set -e

MAKE_BIN=$1
SRC_DIR=$2
OUTPUT_DIR=$3
OUTPUT=$4
shift 4

if [ ${OUTPUT_DIR} = ${SRC_DIR} ]
then
	return;
fi

BUILD_DIR=${OUTPUT_DIR}/${OUTPUT}.p

mkdir -p ${BUILD_DIR} && \
	cp -r ${SRC_DIR}/* ${BUILD_DIR}/

${MAKE_BIN} -C ${BUILD_DIR} $@

cp ${BUILD_DIR}/${OUTPUT} ${OUTPUT_DIR}/${OUTPUT} && \
for f in `ls ${SRC_DIR}`
do
	rm -rf ${BUILD_DIR}/$f
done
