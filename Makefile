
docker-bash:
	docker exec -it pintos bash

docker-container: docker-image
	docker run --detach --name pintos --volume ${PWD}/pintos:/pintos johnstarich/pintos

docker-image:
	docker pull johnstarich/pintos

clean-docker-container:
	docker container stop pintos
	docker container rm pintos


