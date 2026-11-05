for (const comparison of document.querySelectorAll('.comparison')){
  const slider = comparison.querySelector('.compare-range');
  if (!slider) continue;

  const update = () => comparison.style.setProperty('--split', `${slider.value}%`);
  slider.addEventListener('input', update);
  update();
}

const comparisonCarousel = document.querySelector('.comparison-carousel');
if (comparisonCarousel) {
  const slides = Array.from(comparisonCarousel.querySelectorAll('[data-comparison-slide]'));
  const index = document.querySelector('.comparison-index');
  const count = document.querySelector('.comparison-count');
  const markers = slides.map((slide, slideIndex) => {
    const marker = document.createElement('button');
    marker.type = 'button';
    marker.setAttribute('aria-label', `Show ${slide.dataset.comparisonName} comparison`);
    marker.addEventListener('click', () => {
      activeIndex = slideIndex;
      showComparison(activeIndex);
    });
    index.insertBefore(marker, count);
    return marker;
  });
  const showComparison = (index) => {
    const activeIndex = (index + slides.length) % slides.length;
    slides.forEach((slide, slideIndex) => { slide.hidden = slideIndex !== activeIndex; });
    markers.forEach((marker, markerIndex) => {
      const active = markerIndex === activeIndex;
      marker.classList.toggle('is-current', active);
      if (active) marker.setAttribute('aria-current', 'true');
      else marker.removeAttribute('aria-current');
    });
    if (count) count.textContent = `${String(activeIndex + 1).padStart(2, '0')} / ${String(slides.length).padStart(2, '0')}`;
  };
  let activeIndex = 0;
  comparisonCarousel.querySelector('.comparison-previous').addEventListener('click', () => {
    activeIndex = (activeIndex - 1 + slides.length) % slides.length;
    showComparison(activeIndex);
  });
  comparisonCarousel.querySelector('.comparison-next').addEventListener('click', () => {
    activeIndex = (activeIndex + 1) % slides.length;
    showComparison(activeIndex);
  });
  showComparison(activeIndex);
}

for (const question of document.querySelectorAll('.faq-question')) {
  question.addEventListener('click', () => {
    const answer = document.getElementById(question.getAttribute('aria-controls'));
    const opening = question.getAttribute('aria-expanded') !== 'true';
    question.setAttribute('aria-expanded', String(opening));
    question.closest('.faq-item').dataset.open = String(opening);
    answer.setAttribute('aria-hidden', String(!opening));
    answer.inert = !opening;
  });
}

const galleryLightbox = document.querySelector('.gallery-lightbox');
if (galleryLightbox) {
  const enlargedImage = galleryLightbox.querySelector('img');
  const closeButton = galleryLightbox.querySelector('button');
  for (const link of document.querySelectorAll('[data-lightbox]')) {
    link.addEventListener('click', (event) => {
      event.preventDefault();
      const thumbnail = link.querySelector('img');
      enlargedImage.src = link.href;
      enlargedImage.alt = thumbnail.alt;
      galleryLightbox.showModal();
    });
  }
  closeButton.addEventListener('click', () => galleryLightbox.close());
  galleryLightbox.addEventListener('click', (event) => {
    if (event.target === galleryLightbox) galleryLightbox.close();
  });
}