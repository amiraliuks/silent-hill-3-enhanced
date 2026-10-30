for (const comparison of document.querySelectorAll('.comparison')){
  const slider = comparison.querySelector('.compare-range');
  if (!slider) continue;

  const update = () => comparison.style.setProperty('--split', `${slider.value}%`);
  slider.addEventListener('input', update);
  update();
}

const comparisonTabs = Array.from(document.querySelectorAll('[role="tab"]'));
for (const [index, tab] of comparisonTabs.entries()) {
  tab.addEventListener('click', () => selectComparisonTab(tab));
  tab.addEventListener('keydown', (event) => {
    let nextIndex = index;
    if (event.key === 'ArrowRight') nextIndex = (index + 1) % comparisonTabs.length;
    else if (event.key === 'ArrowLeft') nextIndex = (index - 1 + comparisonTabs.length) % comparisonTabs.length;
    else if (event.key === 'Home') nextIndex = 0;
    else if (event.key === 'End') nextIndex = comparisonTabs.length - 1;
    else return;
    event.preventDefault();
    selectComparisonTab(comparisonTabs[nextIndex]);
    comparisonTabs[nextIndex].focus();
  });
}

function selectComparisonTab(activeTab) {
  for (const tab of comparisonTabs) {
    const active = tab === activeTab;
    tab.setAttribute('aria-selected', String(active));
    tab.tabIndex = active ? 0 : -1;
    document.getElementById(tab.getAttribute('aria-controls')).hidden = !active;
  }
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