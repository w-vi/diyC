FROM debian


RUN apt-get update && apt-get install -y software-properties-common \
				python \ 
				curl


