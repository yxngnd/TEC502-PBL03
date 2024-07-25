# Etapa 1: Construção
FROM alpine:latest AS builder

# Instale as dependências necessárias
RUN apk add --no-cache g++ make

# Configure o ambiente de trabalho
WORKDIR /.

# Copie o código fonte para o diretório de trabalho no container
COPY . .

# Compile o código
RUN g++ -o clock clock.cpp -pthread

# Etapa 2: Imagem final
FROM alpine:latest

# Instale apenas as dependências de execução
RUN apk add --no-cache libstdc++

# Configure o ambiente de trabalho
WORKDIR /.

# Copie o binário compilado da etapa de construção
COPY --from=builder /clock .

# Defina o comando padrão para executar o aplicativo
CMD ["./clock"]