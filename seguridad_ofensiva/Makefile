# Toolkit de ataque - Rol 4 (seguridad ofensiva)
# Proyecto 2, CI-4835 Redes de Computadoras I

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -Icomun
LDLIBS_CURL = -lcurl
LDLIBS_GRPC = -lnghttp2 -lssl -lcrypto

COMUN = comun/common.c

REST_BIN = rest_bola rest_mass_assignment rest_auth_debil rest_replay
GQL_BIN  = graphql_introspection graphql_depth_bomb graphql_alias_batch
GRPC_BIN = grpc_reflection grpc_bypass_gateway grpc_proto_decode
TODOS = $(REST_BIN) $(GQL_BIN) $(GRPC_BIN)

all: $(TODOS)
rest: $(REST_BIN)
graphql: $(GQL_BIN)
grpc: $(GRPC_BIN)

rest_bola: rest/rest_bola.c $(COMUN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS_CURL)
rest_mass_assignment: rest/rest_mass_assignment.c $(COMUN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS_CURL)
rest_auth_debil: rest/rest_auth_debil.c $(COMUN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS_CURL)
rest_replay: rest/rest_replay.c $(COMUN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS_CURL)

graphql_introspection: graphql/graphql_introspection.c $(COMUN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS_CURL)
graphql_depth_bomb: graphql/graphql_depth_bomb.c $(COMUN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS_CURL)
graphql_alias_batch: graphql/graphql_alias_batch.c $(COMUN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS_CURL)

grpc_reflection: grpc/grpc_reflection.c grpc/grpc_wire.c $(COMUN)
	$(CC) $(CFLAGS) -Igrpc -o $@ $^ $(LDLIBS_GRPC)
grpc_bypass_gateway: grpc/grpc_bypass_gateway.c grpc/grpc_wire.c $(COMUN)
	$(CC) $(CFLAGS) -Igrpc -o $@ $^ $(LDLIBS_GRPC)
grpc_proto_decode: grpc/grpc_proto_decode.c $(COMUN)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TODOS) results.csv

.PHONY: all rest graphql grpc clean