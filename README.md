# IMPORTANTE LEERME 

cada que quieran alterar entre modo vulnerable y blindado existen dos varuables bien importantes dentro del .env.example:m

# Protegido: SECURITY_MODE=blindado
# Vulnerable: SECURITY_MODE=vulnerable

Cambiando esntre esos modos podemos proteger o blindar el sistema, una vez las cambiemos debemos recrear los contenedores,, por lo que deben correr: 

docker compose up -d --build --force-recreate

Asi el nuevo contenedor toma en cuenta las nuevas variuables de entorno ">u<>"