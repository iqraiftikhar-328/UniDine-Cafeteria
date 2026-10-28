# UniDine — University Cafeteria System

A full-stack cafeteria ordering system with a modern HTML/CSS/JS frontend and a C++ backend. 
The frontend is fully deployed on GitHub Pages and features a built-in JavaScript fallback system, allowing it to calculate bills and process orders even without the C++ server running.

## 🌐 Live Demo
- **Frontend:** https://iqraftikhar-328.github.io/UniDine-Cafeteria/

## ✨ Features
- 🍔 Interactive menu with images and search
- 🛒 Real-time cart with quantity adjustment
- 💰 Server-side price calculation (C++) with **JavaScript fallback**
- 🧾 Receipt generation with real order IDs
- 📊 Daily sales summary (revenue, top items, order log)
- 👤 Student / Guest login with student discount
- 🌙 Dark/light mode toggle
- 📍 Order tracking with status timeline
- ⭐ Feedback system
- 🔥 Combo deals panel
- 📱 Fully responsive

## 🏗️ Tech Stack
| Layer | Technology |
|-------|-----------|
| Frontend | HTML5, CSS3, Vanilla JS |
| Backend | C++17 (raw BSD sockets) |
| Deployment | GitHub Pages (Frontend) |

## 📂 Project Structure
UniDine-Cafeteria/
├── frontend/ # Development copy of the frontend
├── backend/ # C++ server (server.cpp) and Dockerfile
├── docs/ # Production build hosted on GitHub Pages
└── README.md

