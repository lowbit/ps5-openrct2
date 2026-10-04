FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates wget curl unzip xz-utils make cmake ninja-build pkg-config python3 git file xxd \
      clang-18 lld-18 llvm-18 libclang-rt-18-dev g++ autoconf automake libtool \
      innoextract ccache patch \
      libsdl2-dev libicu-dev libzip-dev libpng-dev libzstd-dev nlohmann-json3-dev libflac-dev \
      libvorbis-dev libssl-dev libcurl4-openssl-dev libfontconfig-dev libfreetype-dev \
 && rm -rf /var/lib/apt/lists/*
ENV PATH=/usr/lib/llvm-18/bin:$PATH
WORKDIR /src
