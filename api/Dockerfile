# Stage 1: build native deps
FROM node:20-alpine AS builder
WORKDIR /app
RUN apk add --no-cache python3 make g++
COPY package*.json ./
RUN npm ci
COPY src/ ./src/

# Stage 2: runtime image (no build tools)
FROM node:20-alpine
WORKDIR /app
COPY --from=builder /app/node_modules ./node_modules
COPY --from=builder /app/src ./src
COPY package.json ./
RUN mkdir -p /app/data
EXPOSE 3000
CMD ["node", "src/index.js"]
