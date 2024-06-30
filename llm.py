from mlx_lm import load, generate
from transformers import AutoTokenizer

# Load the model and tokenizer
model_id = "mlx-community/aya-23-8B-4bit"
model, tokenizer = load(model_id)

# Format message with the command-r-plus chat template
messages = [{"role": "user", "content": "Anneme onu ne kadar sevdiğimi anlatan bir mektup yaz"}]
prompt = tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)

# Generate text
generated_text = generate(
    model,
    prompt,
    tokenizer,
    max_tokens=100,
    temperature=0.3,
)

print(generated_text)
