#################################################################
#								#
# Copyright (c) 2018-2020 YottaDB LLC and/or its subsidiaries.	#
# All rights reserved.						#
#								#
#	This source code contains the intellectual property	#
#	of its copyright holder(s), and is made available	#
#	under a license.  If you do not know the terms of	#
#	the license, please stop and do not read further.	#
#								#
#################################################################
# See README.md for more information about this Dockerfile
# Simple build/running directions are below:
#
# Build:
#   $ docker build -t yottadb/yottadb:latest .
#
# Use with data persistence:
#   $ docker run --rm -v `pwd`/ydb-data:/data -ti yottadb/yottadb:latest

ARG OS_VSN=buster

# Stage 1: YottaDB build image
FROM debian:${OS_VSN} as ydb-release-builder

ARG CMAKE_BUILD_TYPE=Release
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y \
                    file \
                    cmake \
                    tcsh \
                    libconfig-dev \
                    libelf-dev \
                    libgcrypt-dev \
                    libgpg-error-dev \
                    libgpgme11-dev \
                    libicu-dev \
                    libncurses-dev \
                    libssl-dev \
                    zlib1g-dev \
                    && \
    apt-get clean

ADD . /tmp/yottadb-src
RUN mkdir -p /tmp/yottadb-build \
 && cd /tmp/yottadb-build \
 && test -f /tmp/yottadb-src/.yottadb.vsn || \
    grep YDB_ZYRELEASE /tmp/yottadb-src/sr_*/release_name.h \
    | grep -o '\(r[0-9.]*\)' \
    | sort -u \
    > /tmp/yottadb-src/.yottadb.vsn \
 && cmake \
      -D CMAKE_INSTALL_PREFIX:PATH=/tmp \
      -D YDB_INSTALL_DIR:STRING=yottadb-release \
      -D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
      /tmp/yottadb-src \
 && make -j $(nproc) \
 && make install

# Stage 2: YottaDB release image
FROM debian:${OS_VSN} as ydb-release

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y \
                    file \
                    binutils \
                    libelf-dev \
                    libicu-dev \
                    locales \
                    wget \
                    vim \
		    procps \
		    make \
		    golang \
		    git \
		    clang \
		    cargo \
                    && \
    apt-get clean
RUN locale-gen en_US.UTF-8
WORKDIR /data
COPY --from=ydb-release-builder /tmp/yottadb-release /tmp/yottadb-release
RUN cd /tmp/yottadb-release  \
 && pkg-config --modversion icu-io \
      > /tmp/yottadb-release/.icu.vsn \
 && ./ydbinstall \
      --utf8 `cat /tmp/yottadb-release/.icu.vsn` \
      --installdir /opt/yottadb/current \
 && rm -rf /tmp/yottadb-release
ENV ydb_dir=/data \
    LANG=en_US.UTF-8 \
    LANGUAGE=en_US:en \
    LC_ALL=C.UTF-8 \
    ydb_chset=UTF-8
# MUPIP RUNDOWN need in the following chain because otherwise ENTRYPOINT complains
# about inability to run %XCMD and rundown needed. Cause not known, but this workaround works
# and is otherwise benign.
RUN . /opt/yottadb/current/ydb_env_set \
 && go get -t lang.yottadb.com/go/yottadb \
 && go test lang.yottadb.com/go/yottadb \
 && git clone https://gitlab.com/oesiman/yottadb-perl.git \
 && cd yottadb-perl \
 && perl Makefile.PL \
 && make test TEST_DB=1 \
 && make install \
 && echo "zsystem \"mupip rundown -relinkctl\"" | /opt/yottadb/current/ydb -dir
ENTRYPOINT ["/opt/yottadb/current/ydb"]
CMD ["-run", "%XCMD", "zsystem \"bash\""]
