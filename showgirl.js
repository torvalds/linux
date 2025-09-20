const releaseDate = new Date("October 3, 2025 00:00:00").getTime();

function updateCountdown() {
  const now = Date.now();
  const distance = releaseDate - now;

  if (distance <= 0) {
    console.clear();
    console.log("🎉 The Life of a Showgirl is Now Released! 🎉");
    clearInterval(timer);
    return;
  }

  const days = Math.floor(distance / (1000 * 60 * 60 * 24));
  const hours = Math.floor((distance % (1000 * 60 * 60 * 24)) / (1000 * 60 * 60));
  const minutes = Math.floor((distance % (1000 * 60 * 60)) / (1000 * 60));
  const seconds = Math.floor((distance % (1000 * 60)) / 1000);

  console.clear();
  console.log(`Countdown to The Life of a Showgirl:`);
  console.log(`${days}d ${hours}h ${minutes}m ${seconds}s`);
}

const timer = setInterval(updateCountdown, 1000)
updateCountdown();
