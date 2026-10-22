# Remixer Tools Desktop App (`remixer-tools`) 🎛️⚡

[![Suite: Remixer Tools](https://img.shields.io/badge/Remixer%20Tools-Desktop%20App-blue.svg)](#)
[![Visibility: Private](https://img.shields.io/badge/Visibility-Private-orange.svg)](#)
[![Developer: Armzi Studio](https://img.shields.io/badge/Developer-Armzi%20Studio-purple.svg)](#)

ตัวโปรแกรม Standalone หลัก (**Desktop UI Shell**) ของชุดเครื่องมือ **Remixer Tools** หน้าจอควบคุมระดับมืออาชีพสำหรับโปรดิวเซอร์และนักดนตรี ครอบคลุม:
- 🎵 **Multi-Track Audio Player & Stem Mixer**: ควบคุมระดับเสียง, Solo/Mute, Pan ของแต่ละ Stem (Vocals, Drums, Bass, Other)
- 🎹 **Piano Roll & MIDI Viewer**: แสดงผลและแก้ไขโน้ตดนตรีที่ได้จากการวิเคราะห์ Audio-to-MIDI
- 📦 **In-App Module Store & Package Manager**: ติดตั้ง, อัปเดต, และจัดการส่วนขยายไฟล์ `.rmxpkg` จากระบบ Cloud Store
- ⚡ **Shared Memory IPC Bridge Host**: สื่อสารและสตรีมสัญญาณเสียง/MIDI สดกับ DAW ผ่าน `remixer-tools-vst-bridge`

---

## 🏛️ แผนผังสถาปัตยกรรมทั้งระบบ (Remixer Tools Ecosystem Matrix)

โครงสร้าง 10 Repositories ภายใต้มาตรฐานชื่อ `remixer-tools-*`:

| Repository | การเข้าถึง (Visibility) | หน้าที่หลัก (Responsibility) | Stack / เทคโนโลยี | สถานะปัจจุบัน |
| :--- | :--- | :--- | :--- | :--- |
| **`remixer-tools`** (Repo นี้) | **Private** | Standalone Application (Desktop UI Shell), Audio Player, Stem Mixer, UI Store | C++ JUCE Framework | 🖥️ Repo หลักสำหรับ Desktop UI (พร้อมใช้งาน) |
| [`remixer-tools-sdk`](../remixer-tools-sdk) | **Public** | C-ABI Headers (.h), Data Structs และ Protocol มาตรฐาน | C11, C++17, CMake | ✅ พร้อมใช้งาน & มีตัวอย่าง C-ABI |
| [`remixer-tools-vst-bridge`](../remixer-tools-vst-bridge) | **Public / Dual** | Audio Plugin Wrapper (VST3, AU, CLAP) รับส่ง Audio/MIDI ผ่าน Shared Memory IPC | C++, JUCE Framework, IPC | ✅ ShmClient Lock-Free พร้อมใช้งาน |
| `remixer-tools-docs` | **Public** | เอกสารแนะนำการพัฒนา Module, API Specs, และ Integration Guide | Markdown, Docusaurus | 📋 อยู่ในแผนพัฒนา |
| [`remixer-tools-downloader`](../remixer-tools-downloader) | **Private** | สกัดและแปลงไฟล์วิดีโอ/เสียงจากสื่อออนไลน์ | C++20, FFmpeg, yt-dlp | ✅ พร้อมใช้งาน & มี CLI Tool |
| [`remixer-tools-core-engine`](../remixer-tools-core-engine) | **Strictly Private** | Core Audio DSP Engine, SPSC Buffer, HWID, Anti-Tamper & Dynamic License Check | C++20, OLLVM, CMake | ✅ คอมไพล์และทดสอบ DSP เรียบร้อย |
| `remixer-tools-audio2midi` | **Strictly Private** | Pitch Detection Algorithm (Audio-to-MIDI) Core IP | C++ / Python, Librosa / Basic Pitch | 📋 สเปกพร้อมใน SDK |
| `remixer-tools-stem-engine` | **Strictly Private** | โหลดและ Decrypt AI Model Weights เข้า RAM ทำ Stem Separation | C++, ONNX Runtime, AES-256 | 📋 สเปกพร้อมใน SDK |
| [`remixer-tools-package-tool`](../remixer-tools-package-tool) | **Strictly Private** | CLI Tool รวบรวม, Obfuscate, เข้ารหัส และออก Digital Signature ไฟล์ `.rmxpkg` | C++20 / Rust, Cryptography | ✅ Pack/Verify/Unpack ผ่านการทดสอบ |
| [`remixer-tools-store-backend`](../remixer-tools-store-backend) | **Strictly Private** | Cloud API Server จัดการ Registry, User Auth, System Licensing, และ CDN Downloads | TypeScript, Node.js, PG, Redis, S3 | ✅ REST API & Schema พร้อมใช้งาน |

---

## 📐 พิมพ์เขียวสถาปัตยกรรม (System Blueprint)

เอกสารสถาปัตยกรรมระบบฉบับสมบูรณ์ (Mermaid Diagrams, Data Flow, Zero-Allocation IPC Protocol, .rmxpkg Format, Licensing Protocol):
- 📖 [System Architecture Blueprint](file:///C:/Users/Armzi/.gemini/antigravity-ide/brain/feef5c7f-b350-49a1-ba04-0ba7a958042e/system_architecture_blueprint.md)

---

## 🚀 วิธีการเปิดใช้งาน (How to Launch)

สามารถเปิดใช้งาน Desktop ได้อย่างสะดวกรวดเร็ว:
1. ดับเบิ้ลคลิกไฟล์ [run_desktop_app.bat](file:///d:/sources/remixer-tools/run_desktop_app.bat) ที่ Root ของโฟลเดอร์ หรือในโฟลเดอร์ [remixer-tools/run_desktop_app.bat](file:///d:/sources/remixer-tools/remixer-tools/run_desktop_app.bat)
2. หรือรันคำสั่งผ่าน Command Prompt / Terminal:
   ```cmd
   d:\sources\remixer-tools\run_desktop_app.bat
   ```
3. หรือเปิดไฟล์ Binary Release โดยตรงที่:
   ```cmd
   d:\sources\remixer-tools\remixer-tools\bin\remixer_desktop_app.exe
   ```

---

## 🚀 ลำดับขั้นตอนการพัฒนา (Roadmap)

1. **Phase 1: Foundation (เสร็จสมบูรณ์แล้ว)**
   - [x] กำหนดมาตรฐานชื่อ `remixer-tools-*` ครบทั้ง 10 Repositories
   - [x] ออกแบบ System Architecture, Data Flow, Security & IPC Protocols
   - [x] วาง C-ABI Headers ใน `remixer-tools-sdk` (`rmx_types.h`, `rmx_audio.h`, `rmx_midi.h`, `rmx_ipc.h`, `rmx_license.h`)
   - [x] พัฒนาและทดสอบ Core Engine DSP + Dynamic Module Loader ใน `remixer-tools-core-engine`
   - [x] พัฒนา Lock-free Shared Memory Client ใน `remixer-tools-vst-bridge`
   - [x] พัฒนา CLI Packager (`rmxpkg`) ใน `remixer-tools-package-tool`
   - [x] วางระบบ Cloud API Server & Licensing Schema ใน `remixer-tools-store-backend`
2. **Phase 2: Desktop UI Shell (`remixer-tools`) (เสร็จสมบูรณ์แล้ว)**
   - [x] พัฒนา Native Windows Dark GUI Shell (Win32 / DWM Immersive Dark Mode)
   - [x] ออกแบบหน้าจอ Modern Dark UI (Waveform Timeline Visualizer, scrubbing, transport controls)
   - [x] Interactive 4-Stem Mixer Console (Vocals, Drums, Bass, Other) พร้อม Real-time dB level, Drag faders, Mute/Solo, Animated 60 FPS Peak VU meters
   - [x] File Dialog Integration รองรับการเปิดไฟล์เสียง (.wav, .mp3, .flac, .m4a)
   - [x] คอมไพล์ MSVC Release Mode พร้อมตัวรัน `run_desktop_app.bat`
3. **Phase 3: Core AI Engines**
   - [ ] Implement `remixer-tools-stem-engine` (ONNX Runtime C++ API + In-RAM AES Decryptor)
   - [ ] Implement `remixer-tools-audio2midi` (Pitch tracking & MIDI quantizer)
4. **Phase 4: Ecosystem & Production Launch**
   - [x] พัฒนา `remixer-tools-downloader` (FFmpeg / yt-dlp media streamer)
   - [ ] Setup Docusaurus ใน `remixer-tools-docs`
   - [ ] เชื่อมต่อ Store Backend กับ Payment Gateway (Stripe)

---

## 📄 ลิขสิทธิ์ (License)

Copyright (c) 2026 Armzi Studio. All rights reserved.