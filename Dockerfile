FROM debian

MAINTAINER redtree0 redtree941@naver.com

RUN apt-get update && apt-get install -y software-properties-common \
				python \ 
				curl


