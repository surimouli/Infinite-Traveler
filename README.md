<!-- PROJECT LOGO -->
<br />
<div align="center">
  <h3 align="center">✈️ The Infinite Traveler</h3>

  <p align="center">
    An autonomous traveler who takes the first possible flight FOREVER.
    <br />
    <br />
    <a href="#about-the-project"><strong>Explore the project »</strong></a>
    <br />
    <br />
    <a href="#usage">View Sample Output</a>
    &middot;
    <a href="https://github.com/your_username/your_repo/issues">Report Bug</a>
    &middot;
    <a href="https://github.com/your_username/your_repo/issues">Request Feature</a>
  </p>
</div>

---

<!-- TABLE OF CONTENTS -->
<details>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-project">About The Project</a>
      <ul>
        <li><a href="#built-with">Built With</a></li>
      </ul>
    </li>
    <li>
      <a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#installation">Installation</a></li>
      </ul>
    </li>
    <li><a href="#usage">Usage</a></li>
    <li><a href="#roadmap">Roadmap</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#contact">Contact</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>

---

## About The Project

**The Infinite Traveler** is an autonomous system that simulates a traveler who never plans ahead.

Starting at **Cincinnati (CVG)**, the traveler:
- always takes the **earliest available flight**
- breaks ties randomly
- waits in **real time** between departures and arrivals
- continues indefinitely

To make this possible for free, the traveler lives **one day behind real time**, replaying real historical flights as if they are happening now.

There is no server, no paid cloud infrastructure, and no always-on machine.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

### Built With

* **C++ (C++20)** — core traveler engine  
* **OpenSky Network API** — real-world flight data (OAuth2)  
* **libcurl** — HTTP requests  
* **GitHub Actions** — free, always-on scheduler  
* **JSON / NDJSON** — persistent state and logs  

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

## Getting Started

This project is designed to run **entirely in the cloud** using GitHub Actions.  
You do **not** need to keep your computer on.

### Prerequisites

* A GitHub account
* An OpenSky Network account (free)
* GitHub repository secrets enabled

### Installation

1. **Clone the repository**
   ```sh
   git clone https://github.com/your_username/your_repo.git

### OpenSky API Setup

1. Log in to **OpenSky Network**
2. Create an **OAuth2 API client**
3. Save the following credentials:
   - `client_id`
   - `client_secret`

### GitHub Secrets

Add the following repository secrets:

- `OPENSKY_CLIENT_ID`
- `OPENSKY_CLIENT_SECRET`

**Path:**  
`Repository → Settings → Secrets and variables → Actions`

### GitHub Actions Permissions

Enable write access for GitHub Actions:

- `Settings → Actions → General`
- Set **Workflow permissions** to **Read and write**

Once committed, the traveler will begin running automatically.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

## Usage

The traveler runs every **5 minutes** via **GitHub Actions**.

Each successful hop produces:

- a new entry in `trip_log.ndjson`
- an updated `state.json`
- an Instagram-ready caption in `latest_caption.txt`

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

### Contact

Mouli Suri
LinkedIn: https://www.linkedin.com/in/moulisuri/

GitHub: https://github.com/surimouli

Project Link: https://github.com/surimouli/Infinite-Traveler

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---

Acknowledgments

- OpenSky Network for open aviation data

- GitHub Actions for free automation

- ADS-B community for flight tracking

- Best-README-Template for inspiration

<p align="right">(<a href="#readme-top">back to top</a>)</p>

---
