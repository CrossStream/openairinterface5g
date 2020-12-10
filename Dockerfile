#!/bin/echo docker build . -f
# -*- coding: utf-8 -*-
# Licence: OAI Public License V1.1

FROM ubuntu:18.04
LABEL maintainer "Philippe Coval (rzr@users.sf.net)"
ENV project openairinterface5g
ENV dir /usr/local/opt/${project}/src/${project}/
ENV TZ=UTC

RUN echo "#log: Setup system for ${project}" \
  && set -x \
  && ln -snf "/usr/share/zoneinfo/$TZ" /etc/localtime \
  && echo "$TZ" > /etc/timezone \
  && apt-get update -y \
  && apt-get install -y \
      --no-install-recommends \
      cmake \
      linux-headers-generic \
      linux-image-generic \
      sudo \
  && rm -rf /var/lib/apt/lists/* \
  && apt-get clean \
  && sync

COPY . ${dir}
WORKDIR ${dir}
RUN echo "#log: Install deps for ${project}" \
  && pwd && ls oaienv \
  && . ./oaienv \
  && cd cmake_targets/ \
  && ./build_oai -I \
  && sync

RUN echo "#log: Building ${project}" \
  && pwd && ls oaienv \
  && . ./oaienv \
  && cd cmake_targets/ \
  && ./build_oai -w USRP --eNB --UE \
  && sync
