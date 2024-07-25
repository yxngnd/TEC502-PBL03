# TEC502-PBL03
Problema 3 da matéria TEC502 - Concorrência e Conectividade: Tempo em Sistemas Distribuídos.

## Como Executar:

A solução para o problema, assim como requerida, está dividida em três sistemas, os quais estão disponilibizados no formate de imagens Docker. Assim, para que seja feito o uso da solução é necessário possuir o Docker instalado na máquina e executar os seguintes comandos:

### Baixar e Executar a Imagem Docker
Execute o seguinte comando para obter a imagem do servidor:
```bash
docker pull yxngnd/clock:latest
```
Execute a imagem substituindo os campos *clock_id* pelo id do relógio, *"ip_address1,ip_address2,ip_addressN"* pelos ips de todos os relógios que estarão conectados, sendo o ip_adress1 o ip da máquina atual, *port* pela porta que será feita a troca de horário e *mport* pela porta que será feita a sincronização com o mestre:

```bash
docker run -e CLOCK_ID=clock_id -e IP_ADDRESSES="ip_address1,ip_address2,ip_addressN" -e PORT=port -e MPORT=mport yxngnd/clock:latest
```
