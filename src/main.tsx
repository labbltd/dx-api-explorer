import { createRoot } from 'react-dom/client';
import Explorer from './explorer';

const root = createRoot(
    document.getElementById('root') as HTMLElement
);

root.render(
    <Explorer></Explorer>
);