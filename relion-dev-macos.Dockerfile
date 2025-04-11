# Development stage
FROM --platform=linux/arm64 ubuntu:22.04 AS dev

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    git \
    build-essential \
    openmpi-bin \
    openmpi-common \
    libopenmpi-dev \
    libfftw3-dev \
    libtiff-dev \
    libpng-dev \
    ghostscript \
    libxft-dev \
    libx11-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Don't copy anything - we'll mount the source code

# Command to run when container starts - can be overridden
CMD ["/bin/bash"]