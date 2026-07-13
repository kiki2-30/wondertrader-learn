# uft-sandbox 编译环境 (仅依赖，不含源码——源码通过 volume 挂载)
FROM --platform=linux/amd64 wondertrader/wondertrader:latest

ADD mydeps_gcc8.4.0.7z /home
RUN cd /home && 7za x mydeps_gcc8.4.0.7z -y && rm mydeps_gcc8.4.0.7z

WORKDIR /home/uft-sandbox
CMD ["/bin/bash"]
