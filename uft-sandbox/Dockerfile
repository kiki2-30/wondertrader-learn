# uft-sandbox 编译环境
# 基于 wondertrader 官方镜像 (x86_64 Ubuntu 18.04, gcc 8.4)
# 添加作者的预编译依赖库 (boost 1.72, spdlog 1.9.2, rapidjson)
FROM --platform=linux/amd64 wondertrader/wondertrader:latest

# 预编译依赖库
ADD mydeps_gcc8.4.0.7z /home
RUN cd /home && 7za x mydeps_gcc8.4.0.7z -y && rm mydeps_gcc8.4.0.7z

WORKDIR /home/uft-sandbox

CMD ["/bin/bash", "-c", "mkdir -p build && cd build && cmake .. && make -j$(nproc)"]
