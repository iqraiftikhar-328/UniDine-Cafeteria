# UniDine — University Cafeteria System

Full-stack cafeteria app: C++ backend + HTML/CSS/JS frontend.

## Live
- Frontend: https://iqraiftikhar-328.github.io/UniDine-Cafeteria/
- Backend: https://unidine-backend.onrender.com/ping

## Features
- Menu with images and search
- Real-time cart
- Server-side price calculation (C++)
- Order receipts, daily summary
- Student/guest login
- Dark/light mode
- Order tracking, feedback

## Run Locally

### Backend
```bash
cd backend
g++ -std=c++17 -o server.exe server.cpp -lws2_32
server.exe